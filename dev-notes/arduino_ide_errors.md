# Arduino IDE 錯誤紀錄與解法

## 2026-02-23: Sketch too big (text section exceeds available space)

### 錯誤訊息
```
Sketch uses 1367995 bytes (104%) of program storage space. Maximum is 1310720 bytes.
Sketch too big; see https://support.arduino.cc/hc/en-us/articles/360013825179 for tips on reducing it.
text section exceeds available space in board
Compilation error: text section exceeds available space in board
```

### 原因
預設的 Partition Scheme 只分配 1.25MB 給程式碼，Firebase 等函式庫太大導致超出。

### 解法
調整兩個 Arduino IDE 設定：

1. **Tools > Partition Scheme > "Huge APP (3MB No OTA/1MB SPIFFS)"**
   - 將程式空間從 1.25MB 提升到 3MB
   - 代價：失去 OTA 更新功能（本專案未使用，無影響）

2. **Tools > Upload Speed > 460800**
   - 加快上傳速度
