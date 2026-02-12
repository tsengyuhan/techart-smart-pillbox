#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // WiFi 網頁設定庫
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <AccelStepper.h>
#include <DHT.h>
#include <DFRobotDFPlayerMini.h>

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

// --- 馬達參數 ---
const int MOVE_STEPS = 200;
const int SENSOR_THRESHOLD = 2400;  // 推桿底部遮斷器門檻
const int STEPS_PER_POSITION = 1067; // 每個位置間隔步數（60度，3200➗ 6）
const int DISPENSE_POSITIONS = 6;   // 總共 6 個位置

// --- 物件宣告 ---
AccelStepper diskMotor(AccelStepper::DRIVER, M1_PUL_PIN, M1_DIR_PIN);
AccelStepper pusherMotor(AccelStepper::DRIVER, M2_PUL_PIN, M2_DIR_PIN);
DHT dht(DHT_PIN, DHT22);
DFRobotDFPlayerMini myDFPlayer;
#define FPSerial Serial1

// ==========================================
// 4. 自訂函式 (Functions)
// ==========================================

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
  
  // 印出五點偵測數值
  Serial.print("📊 五點感測 ADC: ");
  Serial.print(currentADC);
  Serial.print(" | 藥杯狀態: ");
  for (int j = 0; j < 5; j++) {
    Serial.print(cupState[j] ? "🟢" : "⚪");
  }
  Serial.println();
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

  // 4. 心跳時間戳記
  json.set("last_seen", (unsigned long)millis());

  // 寫入 Database
  Firebase.RTDB.updateNode(&fbdo, "/pillbox/monitor", &json);
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
    Serial.println("開始回歸原點...");
    
    // ===== 階段 1: 推桿下降至底部 =====
    Serial.println("  階段 1: 推桿下降");
    
    // 步驟 1.1: 快速下降直到觸發感測器
    pusherMotor.setSpeed(500);
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        break;
      }
      pusherMotor.runSpeed();
    }
    
    // 步驟 1.2: 後退一點點（離開觸發區）
    pusherMotor.move(-100);
    while (pusherMotor.distanceToGo() != 0) pusherMotor.run();
    delay(100);
    
    // 步驟 1.3: 慢速精確歸零
    pusherMotor.setSpeed(100);  // 慢速
    while (true) {
      if (analogRead(SENSOR2_PIN) > SENSOR_THRESHOLD) {
        pusherMotor.stop();
        pusherMotor.setCurrentPosition(0);
        Serial.println("  ✓ 推桿已精確歸零");
        break;
      }
      pusherMotor.runSpeed();
    }
    
    // ===== 階段 2: 圓盤順時針旋轉至原點 =====
    Serial.println("  階段 2: 圓盤旋轉");
    
    // 步驟 2.1: 快速旋轉直到觸發感測器
    diskMotor.setSpeed(500);
    while (true) {
      if (analogRead(SENSOR1_PIN) > SENSOR_THRESHOLD) {
        diskMotor.stop();
        break;
      }
      diskMotor.runSpeed();
    }
    
    // 步驟 2.2: 後退一點點（離開觸發區）
    diskMotor.move(-100);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    delay(100);
    
    // 步驟 2.3: 慢速精確歸零
    diskMotor.setSpeed(100);  // 慢速
    while (true) {
      if (analogRead(SENSOR1_PIN) > SENSOR_THRESHOLD) {
        diskMotor.stop();
        Serial.println("  ✓ 圓盤觸發感測器");
        break;
      }
      diskMotor.runSpeed();
    }
    
    // 步驟 2.4: 後退到真正原點
    diskMotor.move(-30);
    while (diskMotor.distanceToGo() != 0) diskMotor.run();
    diskMotor.setCurrentPosition(0);
    Serial.println("  ✓ 圓盤已精確歸零");
    
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

    int pusherSteps = 3800;
    
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
    myDFPlayer.volume(5);
  }

  // --- 馬達初始化 (降速以配合 2A 電源) ---
  diskMotor.setMaxSpeed(500);
  diskMotor.setAcceleration(100);
  pusherMotor.setMaxSpeed(500);
  pusherMotor.setAcceleration(100);

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


  // --- Firebase 初始化 ---
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
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
  }

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
}