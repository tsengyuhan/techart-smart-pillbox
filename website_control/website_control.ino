#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <AccelStepper.h>
#include <DHT.h>
#include <DFRobotDFPlayerMini.h>

// ==========================================
// 1. 網路與 Firebase 設定
// ==========================================
#define WIFI_SSID "TP-Link_2.4G"
#define WIFI_PASSWORD "0910142371"
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
const float R_WEIGHTS[5] = { 33000.0, 15000.0, 8200.0, 3780.0, 1860.0 };
bool cupState[5] = { false };

// --- 指令過濾器 ---
String lastCommandID = "";

// --- 單點霍爾感測器 ---
const int HALL_THRESHOLD = 1500;  // 根據實測調整
bool movingCupState = false;

// --- 馬達參數 ---
const int MOVE_STEPS = 200;
const int SENSOR_THRESHOLD = 2400;  // 推桿底部遮斷器門檻

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
    movingCupState = true;
  } else {
    movingCupState = false;
  }

  // B. 五點感測 (分壓解碼)
  long sum = 0;
  for (int i = 0; i < 10; i++) sum += analogRead(PIN_5_POINT_SENSOR);
  int currentADC = sum / 10;

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
    myDFPlayer.volume(15);
  }

  // --- 馬達初始化 (降速以配合 2A 電源) ---
  diskMotor.setMaxSpeed(500);
  diskMotor.setAcceleration(100);
  pusherMotor.setMaxSpeed(500);
  pusherMotor.setAcceleration(100);

  // --- 網路初始化 ---
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("連線 WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" 已連線");

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