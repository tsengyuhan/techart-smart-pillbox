/*
 * LED 燈條測試程式
 *
 * 功能：連接 WiFi + Firebase，接收網頁指令控制 LED 燈條開關
 * 使用方式：
 *   1. 上傳至 ESP32
 *   2. 首次使用請連線 WiFi 熱點 "LED-Test-Setup" 設定 WiFi
 *   3. 開啟現有網頁 → 設定 → 開發者手動控制（密碼 1234）
 *   4. 按「燈條 開」/「燈條 關」按鈕控制 LED
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include "time.h"

// ==========================================
// NTP 對時設定
// ==========================================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;  // UTC+8 台灣
const int daylightOffset_sec = 0;

// ==========================================
// Firebase 設定
// ==========================================
#define API_KEY "AIzaSyBbp0kENACTRcVmV2PZW8Q2pHNtMdGhbZ0"
#define DATABASE_URL "smart-pillbox-23113-default-rtdb.firebaseio.com"

// ==========================================
// 硬體腳位
// ==========================================
#define LED_STRIP_PIN 12

// ==========================================
// 全域變數
// ==========================================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;
String lastCommandID = "";
unsigned long lastUploadTime = 0;

// ==========================================
// Setup
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(2000);  // 等待電源穩定

  // --- LED 初始化 ---
  pinMode(LED_STRIP_PIN, OUTPUT);
  digitalWrite(LED_STRIP_PIN, LOW);
  Serial.println("💡 LED 燈條測試程式啟動");

  // --- WiFi 連線 ---
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);

  Serial.println("🌐 嘗試連線 WiFi...");
  Serial.println("如需設定 WiFi，請連線到熱點：LED-Test-Setup");

  if (!wifiManager.autoConnect("LED-Test-Setup")) {
    Serial.println("❌ WiFi 連線逾時，重新啟動...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("✅ WiFi 已連線");
  Serial.print("IP 位址: ");
  Serial.println(WiFi.localIP());

  // --- Firebase 初始化 ---
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  fbdo.setResponseSize(4096);

  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  firebaseReady = true;

  // 讀取舊指令 ID，避免開機後重複執行
  Serial.println("🔍 檢查雲端是否有舊指令...");
  if (Firebase.RTDB.getString(&fbdo, "/pillbox/command")) {
    String oldCommand = fbdo.stringData();
    if (oldCommand != "" && oldCommand.indexOf(',') > 0) {
      int commaIndex = oldCommand.indexOf(',');
      lastCommandID = oldCommand.substring(commaIndex + 1);
      Serial.print("⚠️  發現舊指令 ID: ");
      Serial.print(lastCommandID);
      Serial.println(" -> 已標記為過濾");
    }
  }

  Firebase.RTDB.setString(&fbdo, "/pillbox/command", "");

  // --- NTP 對時 ---
  Serial.println("🌐 同步時間中...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("✅ 時間同步成功");
  } else {
    Serial.println("❌ 時間同步失敗");
  }

  Serial.println("✨ 系統就緒，等待 LED 指令...");
}

// ==========================================
// Loop
// ==========================================
void loop() {
  if (!firebaseReady || WiFi.status() != WL_CONNECTED) return;

  // --- 每 3 秒上傳心跳，讓網頁知道 ESP32 在線 ---
  if (millis() - lastUploadTime >= 3000) {
    lastUploadTime = millis();
    time_t now;
    time(&now);
    Firebase.RTDB.setInt(&fbdo, "/pillbox/monitor/last_seen", (unsigned long)now);
  }

  // 檢查雲端指令
  if (Firebase.RTDB.getString(&fbdo, "/pillbox/command")) {
    String rawData = fbdo.stringData();

    if (rawData != "" && rawData.indexOf(',') > 0) {
      int commaIndex = rawData.indexOf(',');
      String cmd = rawData.substring(0, commaIndex);
      String id = rawData.substring(commaIndex + 1);

      if (id != lastCommandID) {
        Serial.print("✅ 收到指令: ");
        Serial.println(cmd);

        if (cmd == "LED_ON") {
          digitalWrite(LED_STRIP_PIN, HIGH);
          Serial.println("💡 LED 開");
        } else if (cmd == "LED_OFF") {
          digitalWrite(LED_STRIP_PIN, LOW);
          Serial.println("💡 LED 關");
        } else {
          Serial.println("⚠️  非 LED 指令，忽略");
        }

        lastCommandID = id;
        Firebase.RTDB.setString(&fbdo, "/pillbox/command", "");
      }
    }
  }
}
