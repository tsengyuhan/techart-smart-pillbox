#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // WiFi 網頁設定庫
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <AccelStepper.h>
#include <DHT.h>
#include <DFRobotDFPlayerMini.h>
#include "time.h"  // NTP Time Sync


// ==========================================
// 1. Firebase 設定（WiFi 改用 WiFiManager 網頁設定）
// ==========================================
#define API_KEY "AIzaSyBbp0kENACTRcVmV2PZW8Q2pHNtMdGhbZ0"
#define DATABASE_URL "smart-pillbox-23113-default-rtdb.firebaseio.com"

// ==========================================
// 2. 硬體腳位定義
// ==========================================
// --- 馬達 1: 旋轉圓盤 ---
#define M1_PUL_PIN 13
#define M1_DIR_PIN 14
#define M1_ENABLE_PIN 21
#define SENSOR1_PIN 3     // 圓盤歸零感測器 (未使用)

// --- 馬達 2: 推桿 ---
#define M2_PUL_PIN 16
#define M2_DIR_PIN 15
#define SENSOR2_PIN 9  // 底部遮斷器 (ADC1)

// --- 環境與特效 ---
#define FAN_PIN 10
#define DHT_PIN 11
#define LED_STRIP_PIN 12
#define DFPLAYER_TX 17
#define DFPLAYER_RX 18

// --- 感測器 ---
#define PIN_5_POINT_SENSOR 4  // (ADC1) 五點分壓
#define PIN_SINGLE_SENSOR 5   // (ADC1) 單點霍爾 (類比)
#define PIN_CAP_SENSOR 8      // [NEW] 電容感應補藥開關

// ==========================================
// 3. 參數與全域變數
// ==========================================
// --- Firebase 物件 ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;

// --- 上傳計時器 ---
unsigned long lastTempUpdate = 0;
const long TEMP_INTERVAL = 3000;  // 溫度每 3 秒更新
unsigned long lastSensorUpdate = 0;
const long SENSOR_INTERVAL = 200;  // 霍爾每 0.2 秒更新

// --- 五點感測器參數 ---
const float R_PULLUP = 4700.0;
const float R_WEIGHTS[5] = { 33000.0, 15000.0, 8200.0, 3900.0, 2000.0 };
bool cupState[5] = { false };

// --- 指令過濾器 ---
String lastCommandID = "";

// --- 單點霍爾感測器 ---
const int HALL_THRESHOLD = 1500;  // 根據實測調整
bool movingCupState = false;

// --- 設定參數 ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800;  // UTC+8
const int   daylightOffset_sec = 0;

// --- 鬧鐘結構 ---
struct Alarm {
  int hour;
  int minute;
  bool enabled;
};
Alarm alarms[5]; // 最多 5 組鬧鐘

// --- 逾時設定 ---
const unsigned long TIMEOUT_REMINDER = 120000; // 2 分鐘
const unsigned long TIMEOUT_RETRACT = 180000;  // 3 分鐘
const unsigned long DELAY_AFTER_TAKE = 15000;  // 15 秒

// --- 系統狀態 ---
bool isRefillMode = false;
bool isDispensing = false;
unsigned long dispenseStartTime = 0;
bool cupTaken = false;
int targetCups = 0; // 目標藥杯數量

// --- 馬達參數 ---
const int MOVE_STEPS = 200;
const int SENSOR_THRESHOLD = 2400;  // 推桿底部遮斷器門檻
const int STEPS_PER_POSITION = 1067; // 每個位置間隔步數
const int DISPENSE_POSITIONS = 6;
const int PUSHER_MAX_STEPS = 4100; // 推桿最大行程 (用於封孔/出藥)

// --- 物件宣告 ---
AccelStepper diskMotor(AccelStepper::DRIVER, M1_PUL_PIN, M1_DIR_PIN);
AccelStepper pusherMotor(AccelStepper::DRIVER, M2_PUL_PIN, M2_DIR_PIN);
DHT dht(DHT_PIN, DHT22);
DFRobotDFPlayerMini myDFPlayer;
#define FPSerial Serial1

// ==========================================
// 4. 自訂函式 (Functions)
// ==========================================

// --- 馬達基礎動作 ---

// 1. 推桿歸零 (回到最底部)
void homePusher() {
  Serial.println("⚙️ 推桿歸零中...");
  pusherMotor.setSpeed(600);
  // 快速下降
  while (analogRead(SENSOR2_PIN) <= SENSOR_THRESHOLD) {
     pusherMotor.move(100); // 向下
     pusherMotor.runSpeed();
  }
  pusherMotor.stop();
  
  // 後退並精確歸零
  pusherMotor.move(-100);
  while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
  
  pusherMotor.setSpeed(100);
  while (analogRead(SENSOR2_PIN) <= SENSOR_THRESHOLD) {
     pusherMotor.move(5);
     pusherMotor.runSpeed();
  }
  pusherMotor.stop();
  pusherMotor.setCurrentPosition(0);
  Serial.println("✅ 推桿已歸零");
}

// 2. 圓盤歸零 (回到 Sensor 1 位置)
void homeDisk() {
  Serial.println("⚙️ 圓盤歸零中...");
  diskMotor.setSpeed(600);
  while (analogRead(SENSOR1_PIN) <= SENSOR_THRESHOLD) {
     diskMotor.move(100);
     diskMotor.runSpeed();
  }
  diskMotor.stop();
  
  // 精確調整
  diskMotor.move(-100);
  while (diskMotor.distanceToGo() != 0) diskMotor.run();
  
  diskMotor.setSpeed(100);
  while (analogRead(SENSOR1_PIN) <= SENSOR_THRESHOLD) {
     diskMotor.move(5);
     diskMotor.runSpeed();
  }
  diskMotor.stop();
  
  // 回到真正的 0 點 (視安裝角度微調，假設 Sensor 觸發點即原點)
  diskMotor.setCurrentPosition(0);
  Serial.println("✅ 圓盤已歸零");
}

// 3. 封孔 (推桿上升到頂，平常待機狀態)
void sealHole() {
  Serial.println("🔒 執行封孔...");
  // 必須先確認在原點，或假設當前為歸零狀態
  if (pusherMotor.currentPosition() > -100) { // 簡單防呆
      homePusher();
  }
  pusherMotor.moveTo(-PUSHER_MAX_STEPS); // 向上
  while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
  Serial.println("✅ 已封孔 (待機)");
}

// 4. 開孔 (推桿下降到底，準備出藥)
void unsealHole() {
  Serial.println("🔓 執行開孔...");
  homePusher(); // 直接執行歸零即可
}

// 5. 補藥模式 (全機歸零)
void enterRefillMode() {
  if (!isRefillMode) {
    Serial.println("♻️ 進入補藥模式 - 全機歸零");
    isRefillMode = true;
    homePusher();
    homeDisk();
    // 停在這裡等待使用者操作完成
  }
}

void exitRefillMode() {
  if (isRefillMode) {
    Serial.println("▶️ 退出補藥模式 - 恢復待機");
    isRefillMode = false;
    sealHole(); // 恢復封孔
  }
}

// --- 更新感測器狀態 ---
void updateSensors() {
  // A. 單點霍爾 (類比讀取)
  int hallVal = analogRead(PIN_SINGLE_SENSOR);
  
  // 如果讀數低於門檻，視為有磁鐵 (請依實際磁鐵極性與感測器型號調整判斷式)
  if (hallVal < HALL_THRESHOLD) {
    /*if (!movingCupState) {  // 狀態改變時才印出
      Serial.print("🧲 霍爾感測器觸發！數值: ");
      Serial.println(hallVal);
    }*/
    movingCupState = true;
  } else {
    /*if (movingCupState) {  // 狀態改變時才印出
      Serial.print("⬜ 霍爾感測器無訊號。數值: ");
      Serial.println(hallVal);
    }*/
    movingCupState = false;
  }



  // B. 五點感測 (分壓解碼) - 使用中位數濾波改善穩定性
  const int SAMPLES = 50;  // 增加取樣次數
  int adcReadings[SAMPLES];
  
  // 收集多次讀數
  for (int i = 0; i < SAMPLES; i++) {
    adcReadings[i] = analogRead(PIN_5_POINT_SENSOR);
    delayMicroseconds(100);  // 微小延遲讓 ADC 穩定
  }
  
  // 氣泡排序（找中位數）
  for (int i = 0; i < SAMPLES - 1; i++) {
    for (int j = 0; j < SAMPLES - i - 1; j++) {
      if (adcReadings[j] > adcReadings[j + 1]) {
        int temp = adcReadings[j];
        adcReadings[j] = adcReadings[j + 1];
        adcReadings[j + 1] = temp;
      }
    }
  }
  
  // 取中位數（去除極端值）
  int currentADC = adcReadings[SAMPLES / 2];

  int bestMatch = 0;
  float minDifference = 10000.0;
  for (int i = 0; i < 32; i++) {
    float totalConductance = 0;
    for (int j = 0; j < 5; j++) {
      if ((i >> j) & 1) totalConductance += (1.0 / R_WEIGHTS[j]);
    }
    float theoreticalADC = 4095.0 * (1.0 / (1.0 + R_PULLUP * totalConductance));
    float diff = abs(currentADC - theoreticalADC);
    if (diff < minDifference) {
      minDifference = diff;
      bestMatch = i;
    }
  }
  for (int j = 0; j < 5; j++) cupState[j] = ((bestMatch >> j) & 1);
  
  // 計算蓋子上目前的藥杯數量
  int detectedCount = 0;
  for (int j = 0; j < 5; j++) {
      if (cupState[j]) detectedCount++;
  }
  
  // 比較目標
  // 若目標為 0，通常代表沒設定，可視為忽略
  bool isMatch = (targetCups > 0 && detectedCount == targetCups);
  
  // 除錯用
  // Serial.printf("蓋子偵測: %d 杯 (目標: %d) -> %s\n", detectedCount, targetCups, isMatch ? "符合" : "不符");
  
  // 印出五點偵測數值
  /*Serial.print("📊 五點感測 ADC: ");
  Serial.print(currentADC);
  Serial.print(" | 藥杯狀態: ");
  for (int j = 0; j < 5; j++) {
    Serial.print(cupState[j] ? "🟢" : "⚪");
  }
  Serial.println();*/
}

// --- 上傳狀態到 Firebase ---
void uploadStatus() {
  if (!firebaseReady || WiFi.status() != WL_CONNECTED) return;

  FirebaseJson json;

  // 1. 溫度
  float t = dht.readTemperature();
  if (!isnan(t)) json.set("temp", t);

  // 2. 藥杯狀態
  String cups = "";
  for (int i = 0; i < 5; i++) {
    cups += (cupState[i] ? "1" : "0");
    if (i < 4) cups += ",";
  }
  json.set("cups", cups);

  // 3. 單點霍爾
  json.set("hall_sensor", movingCupState);

  // 4. 心跳時間戳記 (NTP Unix timestamp)
  time_t now;
  time(&now);
  json.set("last_seen", (unsigned long)now);

  // 5. 蓋子偵測資訊
  // 計算目前數量
  int count = 0;
  for (int i=0; i<5; i++) if(cupState[i]) count++;
  
  json.set("lid/count", count);
  json.set("lid/target", targetCups);
  json.set("lid/is_match", (count == targetCups));
  
  // 6. 補藥模式狀態
  json.set("refill_mode", isRefillMode);

  // 寫入 Database到 monitor 節點
  Firebase.RTDB.updateNode(&fbdo, "/pillbox/monitor", &json);
}

// --- 解析 Firebase 鬧鐘設定 ---
// 預期格式: 字串 "08:00,12:30,18:00" (以逗號分隔)
void updateAlarmsFromFirebase() {
  // 1. 讀取鬧鐘字串
  if (Firebase.RTDB.getString(&fbdo, "/pillbox/config/alarms_str")) {
     String raw = fbdo.stringData();
     Serial.print("⏰ 更新鬧鐘設定: ");
     Serial.println(raw);
     
     // 清空舊設定
     for(int i=0; i<5; i++) alarms[i].enabled = false;
     
     int alarmIdx = 0;
     int strIdx = 0;
     while (alarmIdx < 5 && strIdx < raw.length()) {
         int comma = raw.indexOf(',', strIdx);
         if (comma == -1) comma = raw.length();
         
         String timeStr = raw.substring(strIdx, comma);
         timeStr.trim();
         
         // 解析 HH:MM
         int colon = timeStr.indexOf(':');
         if (colon > 0) {
             int h = timeStr.substring(0, colon).toInt();
             int m = timeStr.substring(colon+1).toInt();
             alarms[alarmIdx].hour = h;
             alarms[alarmIdx].minute = m;
             alarms[alarmIdx].enabled = true;
             alarmIdx++;
         }
         
         strIdx = comma + 1;
     }
  }
  
  // 2. 讀取目標藥杯數
  if (Firebase.RTDB.getInt(&fbdo, "/pillbox/config/target_cups")) {
      targetCups = fbdo.intData();
      // Serial.print("🎯 更新目標藥杯數: ");
      // Serial.println(targetCups);
  }
}

// --- 出藥流程 (核心邏輯) ---
void startDispenseSequence(int cupIndex) {
  if (isDispensing) return;
  isDispensing = true;
  
  Serial.println("💊 開始定時出藥流程");
  
  // 1. 開孔 (推桿下降)
  unsealHole();
  
  // 2. 轉到指定藥杯位置
  // 假設 cupIndex 1-5，轉動步數需計算
  // 歸零後是 Position 0 (圓片)，Position 1 是第一個藥杯
  int stepsToMove = cupIndex * STEPS_PER_POSITION; 
  Serial.print("  → 轉動到藥杯 ");
  Serial.println(cupIndex);
  diskMotor.move(stepsToMove); 
  while (diskMotor.distanceToGo() != 0) diskMotor.run();
  
  // 3. 推桿上升 (出藥)
  Serial.println("  → 推桿上升 (出藥)");
  pusherMotor.moveTo(-PUSHER_MAX_STEPS);
  while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
  
  // 4. 播放提示音
  myDFPlayer.play(1);
  
  // 5. 監控取藥 (進入等待迴圈)
  unsigned long waitStart = millis();
  bool reminderPlayed = false;
  bool cupRemoved = false;
  
  while (true) {
    unsigned long elapsed = millis() - waitStart;
    
    // 檢查霍爾感測器 (有無藥杯)
    // 注意：原本邏輯是 "低於門檻 = 有磁鐵"
    int hallVal = analogRead(PIN_SINGLE_SENSOR);
    bool hasCup = (hallVal < HALL_THRESHOLD);
    
    // 狀態 A: 藥杯被拿走
    if (!hasCup) {
      Serial.println("✨ 偵測到藥杯取走！等待 15 秒...");
      delay(DELAY_AFTER_TAKE); // 等待 15 秒確認
      cupRemoved = true;
      break; 
    }
    
    // 狀態 B: 超時 2 分鐘 -> 播放提醒
    if (elapsed > TIMEOUT_REMINDER && !reminderPlayed) {
      Serial.println("🔔 超時 2 分鐘 - 播放提醒音");
      myDFPlayer.play(2); // 假設音軌 2 是提醒
      reminderPlayed = true;
    }
    
    // 狀態 C: 超時 3 分鐘 -> 縮回
    if (elapsed > TIMEOUT_RETRACT) {
      Serial.println("⚠️ 超時 3 分鐘 - 自動回收");
      break; // 退出迴圈，執行回收
    }
    
    delay(100); // 避免過度佔用
  }
  
  // 6. 結束流程 (回收推桿 -> 圓盤歸零 -> 封孔)
  Serial.println("  → 流程結束 - 系統復歸");
  homePusher();
  homeDisk();
  sealHole();
  
  isDispensing = false;
}

// --- 檢查鬧鐘 ---
void checkAlarms() {
  if (isRefillMode || isDispensing) return;
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    // Serial.println("無法取得時間");
    return;
  }

  // 檢查每一組鬧鐘
  for (int i = 0; i < 5; i++) {
    if (alarms[i].enabled) {
      if (timeinfo.tm_hour == alarms[i].hour && timeinfo.tm_min == alarms[i].minute && timeinfo.tm_sec == 0) {
        Serial.print("⏰ 鬧鐘觸發: ");
        Serial.println(i);
        
        // 簡單邏輯：每次鬧鐘觸發，出「下一個」有藥的杯子
        // 這裡需要一個變數記錄 "Next Cup Index" 或是即時掃描哪裡有藥
        // 暫時預設：總是出第 1 杯 (需再優化選擇邏輯)
        startDispenseSequence(1); 
        
        delay(1000); // 避免 1 秒內重複觸發
      }
    }
  }
}

void executeCommand(String cmd) {
  Serial.print("執行指令: ");
  Serial.println(cmd);

  // --- 接著才開始做動作 (阻塞式) ---

  if (cmd == "M1_CW") {
    diskMotor.move(MOVE_STEPS);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
  } else if (cmd == "M1_CCW") {
    diskMotor.move(-MOVE_STEPS);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
  } else if (cmd == "M2_UP") {
    pusherMotor.move(-MOVE_STEPS);
    while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
  } else if (cmd == "M2_DOWN") {
    pusherMotor.move(MOVE_STEPS);
    while (pusherMotor.distanceToGo() != 0) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        pusherMotor.setCurrentPosition(0);
        break;
      }
      pusherMotor.run();
    }
  } else if (cmd == "HOME") {
    Serial.println("🏠 執行一鍵歸零 (含封孔)");
    homePusher();
    homeDisk();
    sealHole();
    Serial.println("✅ 回歸原點完成");
  } else if (cmd == "TEST_DISPENSE") {
    Serial.println("🧪 開始出藥測試...");
    
    // 步驟 1: 回歸原點
    Serial.println("　步驟 1: 回歸原點");
    
    // 推桿歸零
    pusherMotor.setSpeed(500);
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        break;
      }
      pusherMotor.runSpeed();
    }
    pusherMotor.move(-100);
    while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
    delay(100);
    pusherMotor.setSpeed(100);
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        pusherMotor.setCurrentPosition(0);
        break;
      }
      pusherMotor.runSpeed();
    }
    
    // 圓盤歸零
    diskMotor.setSpeed(500);
    while (true) {
      if (analogRead(SENSOR1_PIN) > SENSOR_THRESHOLD) {
        diskMotor.stop();
        break;
      }
      diskMotor.runSpeed();
    }
    diskMotor.move(-100);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    delay(100);
    diskMotor.setSpeed(100);
    while (true) {
      if (analogRead(SENSOR1_PIN) > SENSOR_THRESHOLD) {
        diskMotor.stop();
        break;
      }
      diskMotor.runSpeed();
    }
    diskMotor.move(-30);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    diskMotor.setCurrentPosition(0);
    Serial.println("　✓ 已回歸原點");

    int pusherSteps = 4100;
    
    // 步驟 2: 循環 6 個位置
    for (int i = 1; i <= DISPENSE_POSITIONS; i++) {
      Serial.print("  位置 ");
      Serial.print(i);
      Serial.print("/");
      Serial.println(DISPENSE_POSITIONS);
      
      // 2.1 轉到下一個位置（第一個位置除外，因為回歸原點就是第一個位置）
      if (i > 1) {
        diskMotor.move(STEPS_PER_POSITION);
        while (diskMotor.distanceToGo() != 0) diskMotor.run();
      }
      
      // 2.2 推桿向上推（出藥動作）+ LED 漸亮 + 音效
      Serial.println("    → 推桿上升（出藥）");
      pusherMotor.move(pusherSteps * -1);  // 負值 = 向上
      
      int totalSteps = abs(pusherSteps);
      int halfSteps = totalSteps / 2;
      bool soundPlayed = false;
      
      // LED 從 0 漸亮到 255
      while (pusherMotor.distanceToGo() != 0) {
        int currentPos = abs(pusherMotor.currentPosition());
        
        // 計算 LED 亮度 (0-255)
        int brightness = map(currentPos, 0, totalSteps, 0, 255);
        brightness = constrain(brightness, 0, 255);
        analogWrite(LED_STRIP_PIN, brightness);
        
        // 走到一半時播放第三個音檔
        if (currentPos >= halfSteps && !soundPlayed) {
          myDFPlayer.play(1);
          soundPlayed = true;
          Serial.println("      ♪ 播放音效");
        }
        
        pusherMotor.run();
      }
      
      // 停留 1 秒（保持 LED 全亮）
      delay(1000);
      
      // 2.3 推桿回到原點 + LED 漸暗
      if (i == DISPENSE_POSITIONS) {
        // 最後一個位置：使用精確歸零
        Serial.println("    → 推桿精確歸零");
        
        // 快速下降直到觸發感測器 + LED 漸暗
        pusherMotor.setSpeed(500);
        int startBrightness = 255;
        unsigned long startTime = millis();
        
        while (true) {
          if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
            pusherMotor.stop();
            break;
          }
          
          // LED 漸暗
          unsigned long elapsed = millis() - startTime;
          int brightness = map(elapsed, 0, 3000, 255, 0);  // 假設 3 秒內完成
          brightness = constrain(brightness, 0, 255);
          analogWrite(LED_STRIP_PIN, brightness);
          
          pusherMotor.runSpeed();
        }
        
        // 後退一點點
        pusherMotor.move(-100);
        while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
        delay(100);
        
        // 慢速精確歸零
        pusherMotor.setSpeed(100);
        while (true) {
          if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
            pusherMotor.stop();
            pusherMotor.setCurrentPosition(0);
            break;
          }
          pusherMotor.runSpeed();
        }
        
        // 確保 LED 完全關閉
        analogWrite(LED_STRIP_PIN, 0);
        
      } else {
        // 前 5 個位置：簡單下降回原點 + LED 漸暗
        Serial.println("    → 推桿歸零");
        pusherMotor.move(pusherSteps);  // 下降回原點
        
        // LED 從 255 漸暗到 0（在下降一半時完全關閉）
        while (pusherMotor.distanceToGo() != 0) {
          // 使用剩餘步數計算亮度
          int remaining = abs(pusherMotor.distanceToGo());
          
          // 在前半段（totalSteps -> totalSteps/2）時從 255 漸暗到 0
          // 在後半段（< totalSteps/2）時保持 0
          int brightness = map(remaining, totalSteps, totalSteps / 2, 255, 0);
          brightness = constrain(brightness, 0, 255);
          analogWrite(LED_STRIP_PIN, brightness);
          
          pusherMotor.run();
        }
        
        // 確保 LED 完全關閉
        analogWrite(LED_STRIP_PIN, 0);
      }
    }
    
    Serial.println("　已完成 6 個位置測試");
    
    // 步驟 3: 再次回歸原點
    Serial.println("　步驟 2: 再次回歸原點");
    
    // 圓盤歸零
    diskMotor.setSpeed(500);
    while (true) {
      if (analogRead(SENSOR1_PIN) > SENSOR_THRESHOLD) {
        diskMotor.stop();
        break;
      }
      diskMotor.runSpeed();
    }
    diskMotor.move(-100);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    delay(100);
    diskMotor.setSpeed(100);
    while (true) {
      if (analogRead(SENSOR1_PIN) > SENSOR_THRESHOLD) {
        diskMotor.stop();
        break;
      }
      diskMotor.runSpeed();
    }
    diskMotor.move(-30);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    diskMotor.setCurrentPosition(0);
    
    Serial.println("✅ 出藥測試完成");
    
  // --- Demo 模式 A: 反轉展示 ---
  } else if (cmd == "DEMO_A") {
    Serial.println("🎬 Demo 模式 A 開始...");
    
    // 步驟 1: 推桿回歸原點
    Serial.println("  → 推桿回歸原點");
    pusherMotor.setSpeed(800);
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        break;
      }
      pusherMotor.runSpeed();
    }
    pusherMotor.move(-100);
    while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
    delay(100);
    pusherMotor.setSpeed(100);
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        pusherMotor.setCurrentPosition(0);
        break;
      }
      pusherMotor.runSpeed();
    }
    
    // 步驟 2: 圓盤反向轉動一個位置
    Serial.println("  → 圓盤反轉一格");
    diskMotor.move(-STEPS_PER_POSITION);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    
    // 步驟 3: 推桿推到最高處
    Serial.println("  → 推桿推到最高處");
    pusherMotor.move(-4100);  // 使用 pusherSteps 數值
    while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
    
    Serial.println("✅ Demo A 完成");
    
  // --- Demo 模式 B: 正轉展示 ---
  } else if (cmd == "DEMO_B") {
    Serial.println("🎬 Demo 模式 B 開始...");
    
    // 步驟 1: 推桿回歸原點
    Serial.println("  → 推桿回歸原點");
    pusherMotor.setSpeed(800);
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        break;
      }
      pusherMotor.runSpeed();
    }
    pusherMotor.move(-100);
    while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
    delay(100);
    pusherMotor.setSpeed(100);
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        pusherMotor.setCurrentPosition(0);
        break;
      }
      pusherMotor.runSpeed();
    }
    
    // 步驟 2: 圓盤正向轉動一個位置
    Serial.println("  → 圓盤正轉一格");
    diskMotor.move(STEPS_PER_POSITION);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    
    // 步驟 3: 推桿推到最高處
    Serial.println("  → 推桿推到最高處");
    pusherMotor.move(-4100);  // 使用 pusherSteps 數值
    while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
    
    Serial.println("✅ Demo B 完成");
    
  } else if (cmd == "FAN_ON") digitalWrite(FAN_PIN, HIGH);
  else if (cmd == "FAN_OFF") digitalWrite(FAN_PIN, LOW);
  else if (cmd == "LED_ON") digitalWrite(LED_STRIP_PIN, HIGH);
  else if (cmd == "LED_OFF") digitalWrite(LED_STRIP_PIN, LOW);
  else if (cmd == "PLAY_MUSIC") myDFPlayer.play(1);
}

// ==========================================
// 5. Setup
// ==========================================
void setup() {
  Serial.begin(115200);

   // 等待電源穩定（解決上電後 WiFi 連線失敗問題）
  delay(2000);

  // --- 硬體初始化 ---
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LED_STRIP_PIN, OUTPUT);
  pinMode(M1_ENABLE_PIN, OUTPUT);
  digitalWrite(M1_ENABLE_PIN, LOW);  // 鎖定圓盤馬達

  // 初始化感測器
  pinMode(PIN_SINGLE_SENSOR, INPUT);  // 類比輸入不用 PULLUP
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_5_POINT_SENSOR, ADC_ATTENDB_MAX);
  analogSetPinAttenuation(PIN_SINGLE_SENSOR, ADC_ATTENDB_MAX);

  dht.begin();

  // --- 音樂初始化 ---
  FPSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (myDFPlayer.begin(FPSerial)) {
    myDFPlayer.volume(15);
    myDFPlayer.loop(false);  // 關閉循環播放
    myDFPlayer.enableLoop();  // 啟用控制
    delay(200);
  }

  // --- 馬達初始化 (降速以配合 2A 電源) ---
  diskMotor.setMaxSpeed(1500);
  diskMotor.setAcceleration(600);
  pusherMotor.setMaxSpeed(1500);
  pusherMotor.setAcceleration(600);
  
  // --- 補藥開關 ---
  pinMode(PIN_CAP_SENSOR, INPUT);

  // --- WiFi 網頁設定初始化 ---
  WiFiManager wifiManager;
  
  // 設定 AP 超時時間（3 分鐘無操作自動關閉）
  wifiManager.setConfigPortalTimeout(180);
  
  // 嘗試連線，失敗則開啟設定頁面
  Serial.println("🌐 嘗試連線 WiFi...");
  Serial.println("如需設定 WiFi，請連線到熱點：SmartPillbox-Setup");
  
  if (!wifiManager.autoConnect("SmartPillbox-Setup")) {
    Serial.println("❌ WiFi 連線逾時，重新啟動...");
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("✅ WiFi 已連線");
  Serial.print("IP 位址: ");
  Serial.println(WiFi.localIP());

  // --- NTP 對時 ---
  Serial.println("🌐 同步時間中...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("✅ 時間同步成功");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  } else {
    Serial.println("❌ 時間同步失敗");
  }

  // --- 開機自動歸零與封孔 ---
  Serial.println("🚀 執行開機自動程序...");
  homePusher();
  homeDisk();
  sealHole();

  // --- Firebase 初始化 ---
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  // 增加 buffer size 避免資料過長錯誤
  fbdo.setResponseSize(4096);
  
  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  firebaseReady = true;

  // 開機時讀取舊指令並記錄 ID（不執行），避免重複執行
  
  Serial.println("🔍 檢查雲端是否有舊指令...");
  if (Firebase.RTDB.getString(&fbdo, "/pillbox/command")) {
    String oldCommand = fbdo.stringData();
    
    if (oldCommand != "" && oldCommand.indexOf(',') > 0) {
      int commaIndex = oldCommand.indexOf(',');
      String oldID = oldCommand.substring(commaIndex + 1);
      
      lastCommandID = oldID;
      Serial.print("⚠️  發現舊指令 ID: ");
      Serial.print(oldID);
      Serial.println(" -> 已標記為過濾");
    }
  }
  
  Firebase.RTDB.setString(&fbdo, "/pillbox/command", "");
  
  Serial.println("✨ 系統就緒：舊指令已過濾，準備接收新指令");
}

// ==========================================
// 6. Loop
// ==========================================
void loop() {
  unsigned long currentMillis = millis();

  // ------------------------------------
  // 任務 1: 感測器讀取與上傳
  // ------------------------------------
  // 每 0.2 秒讀取一次感測器 (本地更新)
  if (currentMillis - lastSensorUpdate > SENSOR_INTERVAL) {
    updateSensors();
    lastSensorUpdate = currentMillis;
  }

  // 每 3 秒上傳一次狀態 (含溫度) 到 Firebase
  if (currentMillis - lastTempUpdate > TEMP_INTERVAL) {
    uploadStatus();  // 將目前所有數值推送到雲端
    lastTempUpdate = currentMillis;
    
    // 順便檢查鬧鐘設定更新 (每 3 秒檢查一次，或可獨立計時)
    updateAlarmsFromFirebase();
  }
  
  // ------------------------------------
  // 任務 1.5: 檢查補藥模式 (Pin 8)
  // ------------------------------------
  // 假設高電位觸發 (視硬體而定，若為觸摸模組通常是 High)
  if (digitalRead(PIN_CAP_SENSOR) == HIGH) {
    if (!isRefillMode) {
       enterRefillMode();
       delay(1000); // 防彈跳
    } else {
       // 如果已經是 Refill Mode，再次觸摸則退出
       exitRefillMode();
       delay(1000);
    }
  }

  // ------------------------------------
  // 任務 1.6: 檢查鬧鐘
  // ------------------------------------
  checkAlarms();

  // ------------------------------------
  // 任務 2: 檢查雲端指令
  // ------------------------------------
  if (firebaseReady && WiFi.status() == WL_CONNECTED) {
    if (Firebase.RTDB.getString(&fbdo, "/pillbox/command")) {
      String rawData = fbdo.stringData();

      if (rawData != "" && rawData.indexOf(',') > 0) {

        int commaIndex = rawData.indexOf(',');
        String cmd = rawData.substring(0, commaIndex);
        String id = rawData.substring(commaIndex + 1);

        if (id != lastCommandID) {
          Serial.print("✅ 收到新指令 ID: ");
          Serial.println(id);

          executeCommand(cmd);  // 執行動作

          lastCommandID = id;

          Firebase.RTDB.setString(&fbdo, "/pillbox/command", "");
        }
      }
    }
  }

  // ------------------------------------
  // 任務 3: 序列埠直接控制（校準模式）
  // ------------------------------------
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input.length() > 0) {
      int steps = input.toInt();
      
      if (steps != 0) {
        Serial.print("🎯 移動推桿 ");
        Serial.print(steps);
        Serial.println(" 步...");
        
        pusherMotor.move(steps);
        while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
        
        Serial.print("📍 當前位置: ");
        Serial.print(pusherMotor.currentPosition());
        Serial.println(" 步 (負數=向上, 正數=向下)");
      } else if (input == "0" || input.equalsIgnoreCase("reset")) {
        pusherMotor.setCurrentPosition(0);
        Serial.println("✅ 推桿位置已歸零");
      }
    }
  }
}