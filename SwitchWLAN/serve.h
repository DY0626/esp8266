/*
 *  serve.h
 *
 *  说明：STA基础配置及服务；连接成功后同时保存到 EEPROM 和 SDK；/clear 同时清除 EEPROM 与 SDK
 */

#ifndef SERVE_H
#define SERVE_H

#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include "init.h"
#include "index.h"

ESP8266WebServer serve(80); // AP服务端口

// STA（客户端）- ESP8266所连接的热点
String STA_SSID;      // WiFi名称
String STA_PASSWORD;  // WiFi密码

// SSID列表暂存（定义处）
String ScanResult;

// 扫描附近SSID
void wifi_scan_again_ssid(int scanNum){
  ScanResult = "[";
  String type;
  Serial.println("");
  Serial.println("");
  Serial.print("正在扫描附近WIFI热点：");
  for(int i = 0; i < scanNum; i++){
    WiFi.encryptionType(i) == ENC_TYPE_NONE ? type = "1" : type = "0";
    ScanResult += "{\"ssid\":\"" + WiFi.SSID(i) + "\"," + "\"dbm\":\"" + WiFi.RSSI(i) + "\"," + "\"type\":\"" + type + "\"},";
    Serial.print(".");
  }
  Serial.println("");
  ScanResult += "{\"end\":true}]";
  Serial.println(ScanResult);
  Serial.println("");
  Serial.println("完成扫描");
  serve.send(200, "text/plain", ScanResult);
}

// 连接热点
void wifi_connecting(){
  Serial.println("接入热点中：");

  // 同时保存到 SDK（临时开启持久化）与 EEPROM
  WiFi.persistent(true);          // 允许把凭据写入 SDK/NVS
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(STA_SSID.c_str(), STA_PASSWORD.c_str());

  for (size_t i = 0; i < 20; i++) { // 最长约10秒
    if (WiFi.status() == WL_CONNECTED){
      Serial.println("");
      Serial.println("热点连接成功");
      led_off(); // 连接成功：熄灭LED

      // 保存到 EEPROM（用于断电恢复）
      if (saveSavedWiFi(STA_SSID, STA_PASSWORD)) {
        Serial.println("WiFi 凭据已保存到 EEPROM");
      } else {
        Serial.println("保存 WiFi 凭据失败（EEPROM 提交失败）");
      }

      // 恢复为不持久化，避免后续无谓写Flash
      WiFi.persistent(false);

      // 更新缓存，让后台自愈与下次上电立刻可用
      g_have_eeprom_creds = true;
      g_saved_ssid = STA_SSID;
      g_saved_pwd  = STA_PASSWORD;

      serve.send(200, "text/plain", "{\"status\":true}");
      return;
    } else {
      delay(500);
      Serial.print(".");
    }
  }

  // 连接超时
  WiFi.persistent(false);
  led_on(); // 保持LED点亮表示未连接
  Serial.println("");
  Serial.println("热点连接超时");
  serve.send(200, "text/plain", "{\"status\":false}");
}

// 首页回调函数
void www_configPage(){
  serve.send(200, "text/html", configPage);
}

// SSID列表回调函数
void www_getData(){
  serve.send(200, "text/plain", ScanResult);
}

// SSID列表刷新函数
void www_getNewData(){
  wifi_scan_again_ssid(WiFi.scanNetworks());
}

// 连接热点函数
void www_handleData(){
  Serial.println("");
  Serial.print("SSID: ");
  Serial.println(serve.arg("ssid"));
  Serial.print("密码: ");
  Serial.println(serve.arg("pwd"));

  STA_SSID = serve.arg("ssid");
  STA_PASSWORD = serve.arg("pwd");
  wifi_connecting();
}

// 服务初始化
void wifi_serve_init(){
  serve.begin();
  serve.on("/", www_configPage);
  serve.on("/getData", www_getData);
  serve.on("/getNewData", www_getNewData);
  serve.on("/ssidData", www_handleData);

  // /clear：同时清 EEPROM 与 SDK/NVS 的 WiFi 凭据，并自动重启
  serve.on("/clear", [](){
    serve.send(200, "text/plain", "clearing EEPROM + SDK WiFi credentials and restarting");
    delay(200);
    clearAllWiFi();
  });

  serve.onNotFound(www_configPage);
}

#endif