# 音訊上傳播放功能 — 可行性評估

## 需求描述

使用者希望能在網頁介面上錄製或上傳 MP3 音檔，上傳後透過藥盒的喇叭播放。

## 目前硬體架構

| 元件 | 說明 |
|------|------|
| **音訊模組** | DFRobotDFPlayerMini，透過 UART（Serial1, TX=GPIO17, RX=GPIO18）與 ESP32-S3 通訊 |
| **播放方式** | DFPlayer 從其自帶的 microSD 卡讀取 MP3 檔案，指令格式為 `myDFPlayer.play(N)` |
| **通訊架構** | 網頁 → Firebase Realtime Database → ESP32（REST API，傳送字串指令） |
| **現有音訊用途** | Track 1：出藥提示音、Track 2：逾時提醒音 |

## 結論：目前架構不可行

### 原因一：DFPlayer Mini 無法接收遠端音檔

DFPlayer Mini 只能播放其 microSD 卡上預存的 MP3 檔案。透過 UART 串口只能發送播放控制指令（播放、暫停、音量等），**無法傳輸音訊資料**。因此，即使網頁上傳了 MP3，也沒有辦法將檔案寫入 DFPlayer 的 SD 卡。

### 原因二：Firebase 不適合傳輸二進位音檔

目前網頁與 ESP32 之間的通訊是透過 Firebase Realtime Database，傳送簡單的字串指令（如 `"PLAY_MUSIC,1708012345"`）。Firebase Realtime Database **不適合儲存和傳輸大型二進位檔案**（如 MP3），且 ESP32 端的接收 buffer 僅 4096 bytes。

### 原因三：喇叭未直接連接 ESP32

喇叭是連接在 DFPlayer Mini 模組上，而非直接接到 ESP32-S3 的 GPIO。因此 ESP32 無法透過 I2S 或 DAC 直接驅動喇叭播放自訂音訊。

## 未來可行方案（供參考）

### 方案 A：手動更換 SD 卡音檔 + 網頁選曲（最簡單，不需改硬體）

- 使用者將 MP3 檔案手動複製到 DFPlayer 的 microSD 卡（命名為 001.mp3、002.mp3 等）
- 網頁新增選曲功能，發送指令讓 ESP32 呼叫 `myDFPlayer.play(N)` 播放指定曲目
- **優點**：不需任何硬體改動
- **缺點**：需要實體操作 SD 卡，無法即時上傳

### 方案 B：ESP32 架設 Web Server + I2S 外接喇叭（需改硬體）

- 在 ESP32-S3 上架設 HTTP Server，接收上傳的音檔存到 SPIFFS/LittleFS
- 外接 I2S DAC 放大模組（如 MAX98357A）+ 喇叭，由 ESP32 直接驅動播放
- **優點**：可實現完整的遠端上傳 + 播放
- **缺點**：需要額外硬體、佔用 ESP32 flash 空間（通常 1-4MB 可用）、開發較複雜

### 方案 C：ESP32-S3 USB Host + 隨身碟（較複雜）

- 利用 ESP32-S3 的 USB Host 功能讀取 USB 隨身碟上的音檔
- 仍需 I2S/DAC 模組驅動喇叭
- **優點**：儲存空間較大
- **缺點**：需額外硬體、USB Host 程式開發較複雜

## 相關程式碼參考

- DFPlayer 初始化：`website_control/website_control.ino` L814-820
- 音訊播放呼叫：`website_control/website_control.ino` L401, L427, L595, L787
- Firebase 通訊設定：`website_control/website_control.ino` L873

---

*評估日期：2026-02-21*
