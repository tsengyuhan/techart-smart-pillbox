/*
 * TTP223 電容觸摸開關測試程式
 *
 * 功能：讀取 GPIO8 接收 TTP223 模組的數位訊號，判斷是否觸碰
 * 用途：測試 TTP223 模組穿透 3D列印 + 壓克力外殼的觸摸效果
 *
 * 硬體接線：
 *   TTP223 VCC → ESP32-S3 3.3V
 *   TTP223 GND → ESP32-S3 GND
 *   TTP223 OUT → ESP32-S3 GPIO8
 *   TTP223 感應墊 → 銅箔膠帶（貼在外殼內側）
 *
 * 使用方式：
 *   1. 上傳至 ESP32-S3
 *   2. 開啟 Serial Monitor (115200 baud)
 *   3. 觀察觸摸前後的數位訊號變化
 */

#include <Arduino.h>

// ==========================================
// 設定
// ==========================================
#define TOUCH_PIN        8       // GPIO8 連接 TTP223 OUT
#define LED_STRIP_PIN    12      // LED 燈條（與主程式相同）
#define READ_INTERVAL    200     // 讀取間隔 (ms)

// ==========================================
// 全域變數
// ==========================================
bool isTouched = false;
bool lastTouched = false;

// ==========================================
// Setup
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TOUCH_PIN, INPUT);       // TTP223 OUT 是數位輸出
  pinMode(LED_STRIP_PIN, OUTPUT);  // LED 燈條
  digitalWrite(LED_STRIP_PIN, LOW);

  Serial.println("========================================");
  Serial.println("  TTP223 電容觸摸開關測試程式");
  Serial.println("========================================");
  Serial.println("硬體接線：");
  Serial.println("  TTP223 VCC → 3.3V");
  Serial.println("  TTP223 GND → GND");
  Serial.println("  TTP223 OUT → GPIO8");
  Serial.println("----------------------------------------");
  Serial.println("請觸摸 TTP223 感應區域或外殼");
  Serial.println("觸摸時會點亮 LED 燈條 (GPIO12)");
  Serial.println("----------------------------------------");
  Serial.println("  數位值\t狀態\t\tLED燈條");
  Serial.println("----------------------------------------");
}

// ==========================================
// Loop
// ==========================================
void loop() {
  // TTP223 輸出數位訊號：HIGH = 觸摸, LOW = 未觸摸
  int digitalValue = digitalRead(TOUCH_PIN);
  isTouched = (digitalValue == HIGH);

  // 狀態改變時印出提示並控制 LED 燈條
  if (isTouched && !lastTouched) {
    Serial.println(">>> 觸摸觸發！LED 燈條點亮 <<<");
    digitalWrite(LED_STRIP_PIN, HIGH);  // 點亮燈條
  } else if (!isTouched && lastTouched) {
    Serial.println(">>> 已放開！LED 燈條關閉 <<<");
    digitalWrite(LED_STRIP_PIN, LOW);   // 關閉燈條
  }

  // 持續印出數值
  Serial.print("  ");
  Serial.print(digitalValue);
  Serial.print("\t\t");
  Serial.print(isTouched ? "觸摸中 ✓" : "未觸摸  ");
  Serial.print("\t\t");
  Serial.println(isTouched ? "ON" : "OFF");

  lastTouched = isTouched;

  delay(READ_INTERVAL);
}
