/*
 *  init.h
 *
 *  说明：AP/DNS 基础配置；双通道（EEPROM + SDK）持久化 + 启动优先恢复 + 后台自愈重连
 */

#ifndef INIT_H
#define INIT_H

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <EEPROM.h>

// ============ 板载LED配置 ============
// 如为高电平点亮，请将 LED_ACTIVE_LOW 改为 0
#ifndef LED_PIN
#define LED_PIN LED_BUILTIN
#endif
#define LED_ACTIVE_LOW 1

static inline void led_on()  { digitalWrite(LED_PIN, LED_ACTIVE_LOW ? LOW  : HIGH); }
static inline void led_off() { digitalWrite(LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW ); }
// =====================================

// 由 serve.h 定义的外部变量和函数声明，避免互相包含
extern String ScanResult;
void wifi_serve_init();

// AP（服务端）- Esp8266所开放的热点
#define AP_SSID "SwitchWLAN"   // 名称
#define AP_PASSWORD ""       // 密码，空为不设置密码
#define AP_MAX_CLIENT 1      // 可接入数，最多4
IPAddress IP(2,2,2,2);       // 服务地址
bool DNS = true;             // 是否启动DNS（接入热点自动弹出配网）
DNSServer dnsServe;

// ============ EEPROM 存储配置 ============
#define EEPROM_SIZE 256
#define EEPROM_ADDR 0
#define CREDS_MAGIC 0x45574C41u // 'EWLA'

struct StoredCreds {
  uint32_t magic;
  char ssid[32];
  char pass[64];
  uint32_t crc;
};

static uint32_t crc32_update(uint32_t crc, uint8_t data) {
  crc ^= data;
  for (uint8_t i = 0; i < 8; i++) {
    crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
  }
  return crc;
}
static uint32_t crc32_calc(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) crc = crc32_update(crc, data[i]);
  return ~crc;
}
static void safe_copy(char* dst, size_t cap, const String& s) {
  size_t n = s.length();
  if (n >= cap) n = cap - 1;
  memcpy(dst, s.c_str(), n);
  dst[n] = '\0';
}

static bool loadSavedWiFi(String& ssid, String& pass) {
  EEPROM.begin(EEPROM_SIZE);
  StoredCreds c;
  EEPROM.get(EEPROM_ADDR, c);
  if (c.magic != CREDS_MAGIC) {
    Serial.println("EEPROM: magic 不匹配，未保存凭据");
    return false;
  }
  uint8_t buf[sizeof(c.ssid) + sizeof(c.pass)];
  memcpy(buf, c.ssid, sizeof(c.ssid));
  memcpy(buf + sizeof(c.ssid), c.pass, sizeof(c.pass));
  uint32_t crc = crc32_calc(buf, sizeof(buf));
  if (crc != c.crc) {
    Serial.println("EEPROM: CRC 校验失败");
    return false;
  }
  if (c.ssid[0] == '\0') {
    Serial.println("EEPROM: SSID 为空");
    return false;
  }
  ssid = String(c.ssid);
  pass = String(c.pass);
  return true;
}

static bool saveSavedWiFi(const String& ssid, const String& pass) {
  EEPROM.begin(EEPROM_SIZE);
  StoredCreds c;
  memset(&c, 0, sizeof(c));
  c.magic = CREDS_MAGIC;
  safe_copy(c.ssid, sizeof(c.ssid), ssid);
  safe_copy(c.pass, sizeof(c.pass), pass);
  uint8_t buf[sizeof(c.ssid) + sizeof(c.pass)];
  memcpy(buf, c.ssid, sizeof(c.ssid));
  memcpy(buf + sizeof(c.ssid), c.pass, sizeof(c.pass));
  c.crc = crc32_calc(buf, sizeof(buf));
  EEPROM.put(EEPROM_ADDR, c);
  bool ok = EEPROM.commit();
  return ok;
}

static void clearSavedWiFi() {
  EEPROM.begin(EEPROM_SIZE);
  StoredCreds c;
  memset(&c, 0, sizeof(c));
  EEPROM.put(EEPROM_ADDR, c);
  EEPROM.commit();
}

// 同时清除 EEPROM 与 SDK/NVS 的 WiFi 凭据并重启
static void clearAllWiFi() {
  Serial.println("清除 EEPROM 与 SDK WiFi 凭据，并重启...");
  // 1) 清 EEPROM 中我们保存的凭据
  clearSavedWiFi();

  // 2) 清 SDK/NVS 中系统凭据（双保险：disconnect(true) + eraseConfig）
  WiFi.persistent(true);
  WiFi.disconnect(true); // true: 擦除 SDK 中保存的凭据
  delay(200);
  ESP.eraseConfig();     // 额外清理 SDK 配置分区
  WiFi.persistent(false);

  // 3) 可选：关闭 WiFi
  WiFi.mode(WIFI_OFF);
  delay(200);

  // 4) 重启
  ESP.restart();
}
// =====================================

// 缓存已保存凭据，供启动和后台自愈使用
static bool g_have_eeprom_creds = false;
static bool g_cache_inited = false;
static String g_saved_ssid, g_saved_pwd;

static void initCredsCache() {
  if (g_cache_inited) return;
  g_cache_inited = true;
  String s, p;
  if (loadSavedWiFi(s, p)) {
    g_have_eeprom_creds = true;
    g_saved_ssid = s;
    g_saved_pwd  = p;
    Serial.print("从 EEPROM 读取到已保存WiFi：");
    Serial.println(g_saved_ssid);
  } else {
    g_have_eeprom_creds = false;
    Serial.println("未从 EEPROM 读取到凭据，稍后尝试使用 SDK 已保存的WiFi 凭据 ...");
  }
}

// 尝试用缓存的 EEPROM 凭据或 SDK 凭据连接
static void tryConnectSaved(bool verbose = true) {
  if (verbose) Serial.println("tryConnectSaved(): 开始自动连接尝试");
  if (g_have_eeprom_creds) {
    if (verbose) {
      Serial.print("使用 EEPROM 凭据连接：");
      Serial.println(g_saved_ssid);
    }
    WiFi.begin(g_saved_ssid.c_str(), g_saved_pwd.c_str());
  } else {
    if (verbose) Serial.println("使用 SDK 已保存的WiFi 凭据进行连接（若存在）");
    WiFi.begin(); // 使用 SDK/NVS 中的凭据
  }
}

// 后台自愈：未连接时，定期自动重试
static void ensure_sta_connected() {
  static unsigned long lastTryMs = 0;
  const unsigned long intervalMs = 8000; // 8 秒
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastTryMs >= intervalMs) {
    lastTryMs = now;
    tryConnectSaved(true);
  }
}

// 首次扫描附近SSID
void wifi_scan_ssid(int scanNum){
  ScanResult = "[";
  String type;
  Serial.println("");
  Serial.print("正在扫描附近WIFI热点：");
  for(int i = 0; i < scanNum; i++){
    WiFi.encryptionType(i) == ENC_TYPE_NONE ? type = "1" : type = "0";
    ScanResult += "{\"ssid\":\"" + WiFi.SSID(i) + "\"," + "\"dbm\":\"" + WiFi.RSSI(i) + "\"," + "\"type\":\"" + type + "\"},";
    Serial.print(".");
  }
  Serial.println("");
  ScanResult += "{\"end\":\"true\"}]";
  Serial.println(ScanResult);
  Serial.println("完成扫描");
  wifi_serve_init();
}

// AP服务初始化（含断电恢复）
void wifi_init(){
  // 开启 STA+AP，默认不写入SDK（仅在实际连接时临时开启，见 serve.h）
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);

  // 启动AP
  WiFi.softAPConfig(IP, IP, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, AP_MAX_CLIENT);
  delay(300);
  Serial.println("");
  Serial.print("AP服务已启动，IP地址为： ");
  Serial.println(WiFi.softAPIP());

  // 初始化缓存并进行首次自动连接尝试
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  initCredsCache();
  tryConnectSaved(true); // 启动时先尝试一次

  // 异步扫描附近网络供前端列表显示
  WiFi.scanNetworksAsync(wifi_scan_ssid);
}

// DNS服务初始化
void wifi_dns_init(){
  if(DNS){
    dnsServe.start((byte)53, "*", IP);
    Serial.println("");
    Serial.println("DNS服务已启动");
  }
}

#endif