/*
 *  SwitchWLAN.ino (Merged with Blinker + Servo + MIOT) — 舵机参数持久化 + 非阻塞动作
 *
 *  - Wi‑Fi 配网/重连：SwitchWLAN (init.h + serve.h)
 *  - 舵机控制与 Blinker/MIOT：Blinker
 *  - 舵机角度参数（max/min/mid）持久化到 EEPROM，开机恢复
 *  - 配网未连接时 LED 慢闪；长按 FLASH(GPIO0) 3s 松开清除 Wi‑Fi+舵机参数 并重启
 *  - 修复“有时不回正”：引入非阻塞状态机，分段平滑回中，避免长 delay 干扰 Servo PWM
 */

#define BLINKER_WIFI
#define BLINKER_MIOT_OUTLET
#include <Servo.h>
#include <Blinker.h>

#include "init.h"        // AP/DNS + EEPROM + 自愈（WiFi）
#include "serve.h"       // HTTP 服务
#include "servo_store.h" // 舵机参数持久化（含 clamp_angle / clearServoSettings）

// ============ 长按 FLASH 清除所有数据 ============
#ifndef FLASH_BTN_PIN
#define FLASH_BTN_PIN 0       // ESP8266 板载 FLASH 按键为 GPIO0（低有效）
#endif
#ifndef FLASH_LONG_MS
#define FLASH_LONG_MS 3000    // 长按阈值：3 秒
#endif
// =================================================

// 点灯密钥
char auth[] = "893ff0e00813";

// Blinker 组件 key（用可写 char 数组，避免旧版库的 -Wwrite-strings 警告）
char KEY_BTN_K[] = "btn_k";
char KEY_BTN_G[] = "btn_g";
char KEY_MAX[]   = "ran_max1";
char KEY_MIN[]   = "ran_min1";
char KEY_MID[]   = "ran_mid1";

// 组件与舵机
BlinkerButton Button1(KEY_BTN_K);
BlinkerButton Button2(KEY_BTN_G);
BlinkerSlider Slider1(KEY_MAX);
BlinkerSlider Slider2(KEY_MIN);
BlinkerSlider Slider3(KEY_MID);
Servo myservo; // D4 (GPIO2)

// 角度参数（默认值；若 EEPROM 有存档会覆盖）
int ran_max1 = 180, ran_min1 = 0, ran_mid1 = 50;
// 小爱默认中间/最大（兼容旧逻辑）
int xa_mid1 = 50;
int xa_max1 = 180;

// 设备开关状态（用于 MIOT 查询）
bool oState = false;

// ===== LED 闪烁与按键状态 =====
static unsigned long g_led_last_ms = 0;
static bool g_led_state = false;
static bool g_clear_feedback = false; // 达到长按阈值后的快速闪烁提示

static bool g_btn_prev = false;
static unsigned long g_btn_down_ms = 0;
// =============================

// ===== 舵机非阻塞动作状态机 =====
enum ServoActState { ACT_IDLE, ACT_MOVING_TO_EDGE, ACT_HOLD_EDGE, ACT_MOVING_TO_MID };
static ServoActState g_act_state = ACT_IDLE;
static unsigned long g_act_ts = 0;
static int g_target_edge = 0;
static int g_target_mid = 50;
static int g_current_angle = 50;

// 可调参数：保持与平滑回中
static const unsigned long HOLD_EDGE_MS = 700;   // 到边后保持时间
static const unsigned long STEP_INTERVAL_MS = 20;
static const int STEP_SIZE_DEG = 3;

// 约束中点，避免过近极限（5~175）
static inline int SANITIZE_MID(int v) {
  v = clamp_angle(v);
  if (v < 5) v = 5;
  if (v > 175) v = 175;
  return v;
}

static void startServoCycleEx(int edgeAngle, int midAngle) {
  g_target_edge = clamp_angle(edgeAngle);
  g_target_mid = SANITIZE_MID(midAngle);
  // 若当前正处于动作中，允许重启流程
  g_act_state = ACT_MOVING_TO_EDGE;
  g_act_ts = millis();
  Serial.printf("[SERVO] startCycle edge=%d mid=%d\n", g_target_edge, g_target_mid);
}

// 兼容旧逻辑：当 ran_mid1==0 且 ran_max1==0 时，使用 xa_* 作为动作目标
static void startServoCycleUseCompat(bool toOn) {
  if (toOn) {
    if (ran_mid1 == 0 && ran_max1 == 0) {
      startServoCycleEx(xa_max1, xa_mid1);
    } else {
      startServoCycleEx(ran_max1, ran_mid1);
    }
    oState = true;
  } else {
    if (ran_mid1 == 0 && ran_max1 == 0) {
      startServoCycleEx(ran_min1, xa_mid1);
    } else {
      startServoCycleEx(ran_min1, ran_mid1);
    }
    oState = false;
  }
}

static void servoActionTick() {
  unsigned long now = millis();
  switch (g_act_state) {
    case ACT_MOVING_TO_EDGE:
      myservo.write(g_target_edge);
      g_current_angle = g_target_edge;
      g_act_state = ACT_HOLD_EDGE;
      g_act_ts = now;
      Serial.printf("[SERVO] ->EDGE %d\n", g_target_edge);
      break;

    case ACT_HOLD_EDGE:
      if (now - g_act_ts >= HOLD_EDGE_MS) {
        g_act_state = ACT_MOVING_TO_MID;
        g_act_ts = now;
        Serial.printf("[SERVO] HOLD done, ->MID %d\n", g_target_mid);
      }
      break;

    case ACT_MOVING_TO_MID: {
      if (abs(g_current_angle - g_target_mid) <= STEP_SIZE_DEG) {
        g_current_angle = g_target_mid;
        myservo.write(g_current_angle);
        g_act_state = ACT_IDLE;
        Serial.printf("[SERVO] at MID %d\n", g_current_angle);
        break;
      }
      if (now - g_act_ts >= STEP_INTERVAL_MS) {
        g_act_ts = now;
        if (g_current_angle < g_target_mid) g_current_angle += STEP_SIZE_DEG;
        else g_current_angle -= STEP_SIZE_DEG;
        g_current_angle = clamp_angle(g_current_angle);
        myservo.write(g_current_angle);
      }
    } break;

    case ACT_IDLE:
    default:
      break;
  }
}
// ========================================

// 回调实现（移除长 delay，使用状态机发起动作）
void button1_callback(const String & state) {
  BLINKER_LOG("get button state: ", state);
  startServoCycleEx(ran_max1, ran_mid1);
}
void button2_callback(const String & state) {
  BLINKER_LOG("get button state: ", state);
  startServoCycleEx(ran_min1, ran_mid1);
}
void miotPowerState(const String & state) {
  BLINKER_LOG("need set power state: ", state);
  if (state == BLINKER_CMD_ON) {
    startServoCycleUseCompat(true);
    BlinkerMIOT.powerState("on");
    BlinkerMIOT.print();
  } else if (state == BLINKER_CMD_OFF) {
    startServoCycleUseCompat(false);
    BlinkerMIOT.powerState("off");
    BlinkerMIOT.print();
  }
}
void miotQuery(int32_t queryCode) {
  BLINKER_LOG("MIOT Query codes: ", queryCode);
  BlinkerMIOT.powerState(oState ? "on" : "off");
  BlinkerMIOT.print();
}
void dataRead(const String & data) {
  BLINKER_LOG("Blinker readString: ", data);
  Blinker.vibrate();
  uint32_t t = millis();
  Blinker.print(t);
  Blinker.print("millis", t);
}

// 滑块：更新 + 标记延迟保存（取消长 delay，避免打断动作）
// 若正在动作，直接覆盖当前角度，以用户控制为先
void slider1_callback(int32_t value) {
  ran_max1 = clamp_angle(value);
  servo_settings_mark_changed(ran_max1, ran_min1, ran_mid1);
  BLINKER_LOG("slider max: ", ran_max1);
}
void slider2_callback(int32_t value) {
  ran_min1 = clamp_angle(value);
  servo_settings_mark_changed(ran_max1, ran_min1, ran_mid1);
  BLINKER_LOG("slider min: ", ran_min1);
}
void slider3_callback(int32_t value) {
  ran_mid1 = clamp_angle(value);
  servo_settings_mark_changed(ran_max1, ran_min1, ran_mid1);
  // 立即把舵机拉到新的中点（不阻塞），停止当前动作
  g_act_state = ACT_IDLE;
  g_target_mid = SANITIZE_MID(ran_mid1);
  g_current_angle = g_target_mid;
  myservo.write(g_target_mid);
  BLINKER_LOG("slider mid: ", ran_mid1);
}

// 兼容旧版 Blinker：使用 begin(auth, ssid, pwd)
static bool blinker_inited = false;
static void blinker_init_and_connect_once() {
  if (blinker_inited) return;

  // 绑定回调与外设
  Button1.attach(button1_callback);
  Button2.attach(button2_callback);
  Slider1.attach(slider1_callback);
  Slider2.attach(slider2_callback);
  Slider3.attach(slider3_callback);

  myservo.attach(D4);
  // 上电后用“已保存的中间角度”复位到位
  g_current_angle = SANITIZE_MID(ran_mid1);
  myservo.write(g_current_angle);

  BlinkerMIOT.attachQuery(miotQuery);
  BlinkerMIOT.attachPowerState(miotPowerState);
  Blinker.attachData(dataRead);

  // 取当前 SDK（或回退 EEPROM）凭据初始化 Blinker
  String ssid = WiFi.SSID();
  String pwd  = WiFi.psk();
  if (ssid.length() == 0) {
    String s, p;
    if (loadSavedWiFi(s, p)) { ssid = s; pwd = p; }
  }
  Blinker.begin(auth, ssid.c_str(), pwd.c_str());
  blinker_inited = true;
  Serial.println("Blinker 初始化完成");
}

// LED 配网/清除 提示：未连接时慢闪；长按达到阈值后快速闪烁
static void led_provisioning_tick() {
  if (WiFi.status() == WL_CONNECTED) {
    // 已联网：熄灭
    led_off();
    g_led_state = false;
    return;
  }
  unsigned long now = millis();
  const unsigned long interval = g_clear_feedback ? 120 : 400; // 清除确认：快闪
  if (now - g_led_last_ms >= interval) {
    g_led_last_ms = now;
    g_led_state = !g_led_state;
    if (g_led_state) led_on(); else led_off();
  }
}

// FLASH 按键长按检测：按住 >= 3s 时进入“快闪确认”，松开后执行清除并重启
static void flash_button_tick() {
  bool pressed = (digitalRead(FLASH_BTN_PIN) == LOW); // 低有效
  unsigned long now = millis();

  if (pressed) {
    if (!g_btn_prev) {
      g_btn_down_ms = now;
    } else {
      if (!g_clear_feedback && now - g_btn_down_ms >= FLASH_LONG_MS) {
        g_clear_feedback = true;
        Serial.println("检测到 FLASH 按键长按（>3s），松开以清除所有数据...");
      }
    }
  } else {
    if (g_clear_feedback) {
      // 松开后真正执行清除
      Serial.println("执行清除所有数据...");
      // 先给一个可见的“确认闪烁”
      for (int i = 0; i < 6; i++) {
        led_on(); delay(80);
        led_off(); delay(80);
      }
      // 清除 & 重启
      // WiFi: EEPROM + SDK/NVS
      clearSavedWiFi();
      WiFi.persistent(true);
      WiFi.disconnect(true);
      delay(200);
      ESP.eraseConfig();
      WiFi.persistent(false);
      // 舵机参数
      clearServoSettings();
      // 重启
      ESP.restart();
    }
  }
  g_btn_prev = pressed;
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  led_on(); // 开机先点亮，随后根据状态由 tick 控制

  // FLASH 按键输入（上拉）
  pinMode(FLASH_BTN_PIN, INPUT_PULLUP);

  // 启动配网/AP + DNS，自愈重连
  wifi_init();
  wifi_dns_init();

  // 读取舵机参数（若无存档则保留默认值 180/0/50）
  int mxa, mna, mia;
  if (loadServoSettings(mxa, mna, mia)) {
    ran_max1 = mxa;
    ran_min1 = mna;
    ran_mid1 = mia;
    Serial.printf("已加载舵机参数: max=%d, min=%d, mid=%d\n", ran_max1, ran_min1, ran_mid1);
  } else {
    Serial.println("未找到舵机参数存档，使用默认值");
  }
}

void loop() {
  // WLAN 服务
  serve.handleClient();
  if (DNS) dnsServe.processNextRequest();

  // WiFi 自愈
  ensure_sta_connected();

  // LED 与 Blinker 初始化（仅在状态变化时打印；LED 由 led_provisioning_tick 控制）
  static wl_status_t last = WL_IDLE_STATUS;
  wl_status_t now = WiFi.status();
  if (now != last) {
    last = now;
    if (now == WL_CONNECTED) {
      Serial.println("LED: OFF (WiFi connected)");
      blinker_init_and_connect_once();
    } else {
      Serial.println("LED: BLINK (WiFi not connected, provisioning)");
    }
  }

  // 配网状态 LED 闪烁 + 长按 FLASH 清除
  flash_button_tick();
  led_provisioning_tick();

  // 舵机动作状态机心跳（非阻塞：平滑回中）
  servoActionTick();

  // 舵机参数延迟保存心跳
  servo_settings_tick();

  if (blinker_inited) Blinker.run();
  delay(10);
}

/*
Blinker App 界面配置（保持原样）
{¨version¨¨2.0.0¨¨config¨{¨headerColor¨¨transparent¨¨headerStyle¨¨dark¨¨background¨{¨img¨¨assets/img/headerbg.jpg¨¨isFull¨«}}¨dashboard¨|{¨type¨¨tex¨¨t0¨¨blinker入门示例¨¨t1¨¨文本2¨¨bg¨Ë¨ico¨´´¨cols¨Í¨rows¨Ê¨key¨¨tex-272¨´x´É´y´É¨speech¨|÷¨lstyle¨Ê¨clr¨¨#FFF¨}{ßC¨btn¨ßJ¨fal fa-power-off¨¨mode¨ÉßE¨关灯¨ßGßHßIÉßKËßLËßM¨btn_g¨´x´Î´y´ÏßPÉ}{ßCßSßJßTßUÉßE¨开灯¨ßGßHßIÉßKËßLËßM¨btn_k¨´x´Ê´y´ÏßPÉ}{ßC¨ran¨ßE¨最大角度¨ßQ¨#389BEE¨¨max¨¢2u¨min¨ÉßIÉßKÑßLËßM¨ran_max1¨´x´É´y´¤DßPÉ}{ßCßZßE¨最小角度¨ßQßbßcº0ßdÉßIÉßKÑßLËßM¨ran_min1¨´x´É´y´ÒßPÉ}{ßCßZßE¨中间角度¨ßQßbßcº0ßdÉßIÉßKÑßLËßM¨ran_mid1¨´x´É´y´¤BßPÉ}{ßC¨deb¨ßUÉßIÉßKÑßLÌßM¨debug¨´x´É´y´Ì}÷¨actions¨|¦¨cmd¨¦¨switch¨‡¨text¨‡¨on¨¨打开?name¨¨off¨¨关闭?name¨—÷¨triggers¨|{¨source¨ßn¨source_zh¨¨开关状态¨¨state¨|ßpßr÷¨state_zh¨|¨打开¨´关闭´÷}÷´rt´|÷}
*/