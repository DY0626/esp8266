/*
 *  SwitchWLAN.ino (Merged with Blinker + Servo + MIOT) — 舵机参数持久化版
 *
 *  - Wi‑Fi 配网/重连：EasyWLAN (init.h + serve.h)
 *  - 舵机控制与 Blinker/MIOT：Blinker
 *  - 舵机角度参数（max/min/mid）持久化到 EEPROM，开机恢复
 */

#define BLINKER_WIFI
#define BLINKER_MIOT_OUTLET
#include <Servo.h>
#include <Blinker.h>

#include "init.h"        // AP/DNS + EEPROM + 自愈（WiFi）
#include "serve.h"       // HTTP 服务
#include "servo_store.h" // 舵机参数持久化

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
Servo myservo; // D4

// 角度参数（默认值；若 EEPROM 有存档会覆盖）
int ran_max1 = 180, ran_min1 = 0, ran_mid1 = 50;
// 小爱默认中间/最大（旧逻辑保留）
int xa_mid1 = 50;
int xa_max1 = 180;

bool oState = false;

// 回调实现
void button1_callback(const String & state) {
  BLINKER_LOG("get button state: ", state);
  myservo.write(ran_max1);
  Blinker.vibrate();
  Blinker.delay(1000);
  myservo.write(ran_mid1);
  Blinker.vibrate();
}
void button2_callback(const String & state) {
  BLINKER_LOG("get button state: ", state);
  myservo.write(ran_min1);
  Blinker.vibrate();
  Blinker.delay(1000);
  myservo.write(ran_mid1);
  Blinker.vibrate();
}
void miotPowerState(const String & state) {
  BLINKER_LOG("need set power state: ", state);
  if (state == BLINKER_CMD_ON && ran_mid1 == 0 && ran_max1 == 0) {
    myservo.write(xa_max1);
    Blinker.delay(1000);
    myservo.write(xa_mid1);
    BlinkerMIOT.powerState("on");
    BlinkerMIOT.print();
  }
  else if (state == BLINKER_CMD_ON) {
    myservo.write(ran_max1);
    Blinker.delay(1000);
    myservo.write(ran_mid1);
    BlinkerMIOT.powerState("on");
    BlinkerMIOT.print();
  }
  else if (state == BLINKER_CMD_OFF && ran_mid1 == 0 && ran_max1 == 0) {
    myservo.write(ran_min1);
    Blinker.delay(1000);
    myservo.write(xa_mid1);
    BlinkerMIOT.powerState("off");
    BlinkerMIOT.print();
  }
  else if (state == BLINKER_CMD_OFF) {
    myservo.write(ran_min1);
    Blinker.delay(1000);
    myservo.write(ran_mid1);
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

// 滑块：更新 + 标记延迟保存
void slider1_callback(int32_t value) {
  ran_max1 = value; myservo.write(ran_max1);
  Blinker.vibrate(); Blinker.delay(1000);
  servo_settings_mark_changed(ran_max1, ran_min1, ran_mid1);
  BLINKER_LOG("slider max: ", value);
}
void slider2_callback(int32_t value) {
  ran_min1 = value; myservo.write(ran_min1);
  Blinker.vibrate(); Blinker.delay(1000);
  servo_settings_mark_changed(ran_max1, ran_min1, ran_mid1);
  BLINKER_LOG("slider min: ", value);
}
void slider3_callback(int32_t value) {
  ran_mid1 = value; myservo.write(ran_mid1);
  Blinker.vibrate(); Blinker.delay(1000);
  servo_settings_mark_changed(ran_max1, ran_min1, ran_mid1);
  BLINKER_LOG("slider mid: ", value);
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
  myservo.write(ran_mid1);

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

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  led_on(); // 未联网：点亮

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

  // LED 与 Blinker 初始化
  static wl_status_t last = WL_IDLE_STATUS;
  wl_status_t now = WiFi.status();
  if (now != last) {
    last = now;
    if (now == WL_CONNECTED) {
      led_off();
      Serial.println("LED: OFF (WiFi connected)");
      blinker_init_and_connect_once();
    } else {
      led_on();
      Serial.println("LED: ON (WiFi not connected)");
    }
  }

  // 舵机参数延迟保存心跳
  servo_settings_tick();

  if (blinker_inited) Blinker.run();
  delay(10);
}

/*
Blinker App 界面配置（保持原样）
{¨version¨¨2.0.0¨¨config¨{¨headerColor¨¨transparent¨¨headerStyle¨¨dark¨¨background¨{¨img¨¨assets/img/headerbg.jpg¨¨isFull¨«}}¨dashboard¨|{¨type¨¨tex¨¨t0¨¨blinker入门示例¨¨t1¨¨文本2¨¨bg¨Ë¨ico¨´´¨cols¨Í¨rows¨Ê¨key¨¨tex-272¨´x´É´y´É¨speech¨|÷¨lstyle¨Ê¨clr¨¨#FFF¨}{ßC¨btn¨ßJ¨fal fa-power-off¨¨mode¨ÉßE¨关灯¨ßGßHßIÉßKËßLËßM¨btn_g¨´x´Î´y´ÏßPÉ}{ßCßSßJßTßUÉßE¨开灯¨ßGßHßIÉßKËßLËßM¨btn_k¨´x´Ê´y´ÏßPÉ}{ßC¨ran¨ßE¨最大角度¨ßQ¨#389BEE¨¨max¨¢2u¨min¨ÉßIÉßKÑßLËßM¨ran_max1¨´x´É´y´¤DßPÉ}{ßCßZßE¨最小角度¨ßQßbßcº0ßdÉßIÉßKÑßLËßM¨ran_min1¨´x´É´y´ÒßPÉ}{ßCßZßE¨中间角度¨ßQßbßcº0ßdÉßIÉßKÑßLËßM¨ran_mid1¨´x´É´y´¤BßPÉ}{ßC¨deb¨ßUÉßIÉßKÑßLÌßM¨debug¨´x´É´y´Ì}÷¨actions¨|¦¨cmd¨¦¨switch¨‡¨text¨‡¨on¨¨打开?name¨¨off¨¨关闭?name¨—÷¨triggers¨|{¨source¨ßn¨source_zh¨¨开关状态¨¨state¨|ßpßr÷¨state_zh¨|¨打开¨´关闭´÷}÷´rt´|÷}
*/