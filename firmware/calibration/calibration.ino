/**
 * 蓋子五點偵測校正工具 v2
 * 
 * 使用方式：
 * 1. 燒錄後開啟 Serial Monitor (115200)
 * 2. 放好藥杯到想要測試的位置
 * 3. 輸入 "1" → 開始記錄
 * 4. 輸入 "0" → 停止，顯示完整統計
 * 5. 重複步驟 2-4，測試不同位置 / 不同杯數
 * 
 * 輸出統計：平均、中位數、最小、最大、標準差
 */

#include <Arduino.h>
#include <math.h>

const int PIN_LID = 4;

bool recording = false;

// 最多取樣 500 筆 (50 秒夠用了)
const int MAX_SAMPLES = 500;
int samples[MAX_SAMPLES];
int sampleCount = 0;
int testNumber = 1;

// 排序用 (計算中位數)
int compareInt(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}

void printStats() {
  if (sampleCount == 0) {
    Serial.println("■ 停止記錄 (無取樣資料)");
    return;
  }

  // 平均值
  long total = 0;
  for (int i = 0; i < sampleCount; i++) {
    total += samples[i];
  }
  float avg = (float)total / sampleCount;

  // 最小值 / 最大值
  int minVal = samples[0], maxVal = samples[0];
  for (int i = 1; i < sampleCount; i++) {
    if (samples[i] < minVal) minVal = samples[i];
    if (samples[i] > maxVal) maxVal = samples[i];
  }

  // 標準差
  float sumSqDiff = 0;
  for (int i = 0; i < sampleCount; i++) {
    float diff = samples[i] - avg;
    sumSqDiff += diff * diff;
  }
  float stdDev = sqrt(sumSqDiff / sampleCount);

  // 中位數 (需要排序)
  qsort(samples, sampleCount, sizeof(int), compareInt);
  float median;
  if (sampleCount % 2 == 0) {
    median = (samples[sampleCount / 2 - 1] + samples[sampleCount / 2]) / 2.0;
  } else {
    median = samples[sampleCount / 2];
  }

  // 輸出
  Serial.println("■ 停止記錄");
  Serial.println("┌──────────────────────────────┐");
  Serial.printf("│  測試 #%d 結果                 \n", testNumber);
  Serial.println("├──────────────────────────────┤");
  Serial.printf("│  取樣數:   %d 筆\n", sampleCount);
  Serial.printf("│  平均值:   %.1f\n", avg);
  Serial.printf("│  中位數:   %.1f\n", median);
  Serial.printf("│  最小值:   %d\n", minVal);
  Serial.printf("│  最大值:   %d\n", maxVal);
  Serial.printf("│  範圍:     %d\n", maxVal - minVal);
  Serial.printf("│  標準差:   %.1f\n", stdDev);
  Serial.println("└──────────────────────────────┘");
  Serial.println();

  testNumber++;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LID, INPUT);

  Serial.println();
  Serial.println("========================================");
  Serial.println("  蓋子五點偵測校正工具 v2 (GPIO 4)");
  Serial.println("========================================");
  Serial.println("操作說明：");
  Serial.println("  放好藥杯後，輸入 1 → 開始記錄");
  Serial.println("  穩定後，  輸入 0 → 停止並顯示統計");
  Serial.println("========================================");
  Serial.println();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    
    if (c == '1' && !recording) {
      recording = true;
      sampleCount = 0;
      Serial.println("▶ 開始記錄中... (輸入 0 停止)");
      
    } else if (c == '0' && recording) {
      recording = false;
      printStats();
    }
  }

  if (recording && sampleCount < MAX_SAMPLES) {
    int val = analogRead(PIN_LID);
    samples[sampleCount] = val;
    sampleCount++;

    // 每 10 次印即時狀態
    if (sampleCount % 10 == 0) {
      long runTotal = 0;
      for (int i = 0; i < sampleCount; i++) runTotal += samples[i];
      float runAvg = (float)runTotal / sampleCount;
      Serial.printf("  [即時] 值: %4d | 平均: %.1f (%d 筆)\n", 
                    val, runAvg, sampleCount);
    }

    delay(100);
  }
}
