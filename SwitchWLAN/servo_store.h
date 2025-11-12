/*
 *  servo_store.h
 *
 *  舵机参数（max/min/mid）持久化到 EEPROM，带 CRC 校验与延迟提交。
 *  与 init.h 的 WiFi 存储相互独立，默认使用 EEPROM 偏移 128。
 */

#ifndef SERVO_STORE_H
#define SERVO_STORE_H

#include <Arduino.h>
#include <EEPROM.h>

// 如果 init.h 已定义 EEPROM_SIZE，则复用；否则给个默认值
#ifndef EEPROM_SIZE
#define EEPROM_SIZE 256
#endif

// 将舵机参数与 WiFi 凭据分开存放：WiFi 用 0 起，舵机用 128 起
#ifndef SERVO_EEPROM_ADDR
#define SERVO_EEPROM_ADDR 128
#endif

// Magic 用 'SRV1'
#define SERVO_MAGIC 0x31565253u

struct ServoSettingsBlob {
  uint32_t magic;
  int16_t max_angle;
  int16_t min_angle;
  int16_t mid_angle;
  uint8_t reserved[8]; // 预留扩展
  uint32_t crc;
};

// 本地 CRC32（避免与 init.h 中函数名冲突）
static uint32_t servo_crc32_update(uint32_t crc, uint8_t data) {
  crc ^= data;
  for (uint8_t i = 0; i < 8; i++) {
    crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
  }
  return crc;
}
static uint32_t servo_crc32_calc(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) crc = servo_crc32_update(crc, data[i]);
  return ~crc;
}

// 简单范围约束
static inline int16_t clamp_angle(int32_t v) {
  if (v < 0) v = 0;
  if (v > 180) v = 180;
  return (int16_t)v;
}

// 读取；成功返回 true，并输出角度
static bool loadServoSettings(int &maxA, int &minA, int &midA) {
  EEPROM.begin(EEPROM_SIZE);
  ServoSettingsBlob b;
  EEPROM.get(SERVO_EEPROM_ADDR, b);
  if (b.magic != SERVO_MAGIC) return false;

  uint8_t buf[sizeof(ServoSettingsBlob) - sizeof(uint32_t)];
  memcpy(buf, &b, sizeof(ServoSettingsBlob) - sizeof(uint32_t));
  uint32_t crc = servo_crc32_calc(buf, sizeof(buf));
  if (crc != b.crc) return false;

  maxA = clamp_angle(b.max_angle);
  minA = clamp_angle(b.min_angle);
  midA = clamp_angle(b.mid_angle);
  return true;
}

// 直接保存一次
static bool saveServoSettings_now(int maxA, int minA, int midA) {
  EEPROM.begin(EEPROM_SIZE);
  ServoSettingsBlob b;
  memset(&b, 0, sizeof(b));
  b.magic = SERVO_MAGIC;
  b.max_angle = clamp_angle(maxA);
  b.min_angle = clamp_angle(minA);
  b.mid_angle = clamp_angle(midA);

  uint8_t buf[sizeof(ServoSettingsBlob) - sizeof(uint32_t)];
  memcpy(buf, &b, sizeof(ServoSettingsBlob) - sizeof(uint32_t));
  b.crc = servo_crc32_calc(buf, sizeof(buf));

  EEPROM.put(SERVO_EEPROM_ADDR, b);
  return EEPROM.commit();
}

// -------- 延迟提交机制（减少写入次数） --------
static bool g_servo_dirty = false;
static unsigned long g_servo_last_change_ms = 0;
static int g_servo_max_p = 0, g_servo_min_p = 0, g_servo_mid_p = 0;
static const unsigned long SERVO_SAVE_DELAY_MS = 2000;

static void servo_settings_mark_changed(int maxA, int minA, int midA) {
  g_servo_max_p = clamp_angle(maxA);
  g_servo_min_p = clamp_angle(minA);
  g_servo_mid_p = clamp_angle(midA);
  g_servo_last_change_ms = millis();
  g_servo_dirty = true;
}

// 在 loop() 周期调用，静默 2s 后自动保存
static void servo_settings_tick() {
  if (!g_servo_dirty) return;
  if (millis() - g_servo_last_change_ms >= SERVO_SAVE_DELAY_MS) {
    saveServoSettings_now(g_servo_max_p, g_servo_min_p, g_servo_mid_p);
    g_servo_dirty = false;
  }
}

// 清除舵机参数（写入空数据，magic 失效）
static void clearServoSettings() {
  EEPROM.begin(EEPROM_SIZE);
  ServoSettingsBlob b;
  memset(&b, 0, sizeof(b));
  EEPROM.put(SERVO_EEPROM_ADDR, b);
  EEPROM.commit();
}

#endif // SERVO_STORE_H