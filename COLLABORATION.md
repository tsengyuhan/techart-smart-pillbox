# 🤝 AI 協作分工指南（Antigravity + Claude）

> **目的**：讓兩個 AI 助手同時修改此專案，避免程式碼衝突。
> **日期**：2026-02-20

---

## 📋 分工總覽

| 負責人 | Git 分支 | 負責檔案 | 工作範圍 |
|--------|----------|----------|----------|
| **Antigravity** | `antigravity/web-ui` | `index.html`, `css/style.css`, `js/ui.js` | 網頁介面設計、UI/UX、畫面美化 |
| **Claude** | `claude/esp32-api` | `website_control.ino`, `js/api.js` | ESP32 韌體、Firebase 通訊、API |

---

## 📁 檔案架構

原本的 `script.js` 已拆分為兩個檔案，避免衝突：

```
control_app/
├── index.html              ← Antigravity 負責（HTML 結構與版面）
├── css/
│   └── style.css           ← Antigravity 負責（所有樣式）
├── js/
│   ├── api.js              ← Claude 負責（Firebase 初始化、資料監聽、指令發送）
│   └── ui.js               ← Antigravity 負責（UI 互動、DOM 操作、動畫）
├── website_control/
│   └── website_control.ino ← Claude 負責（ESP32 韌體）
├── COLLABORATION.md        ← 本文件（分工說明）
├── 規格書3.0.md             ← 共用參考文件（勿修改）
└── 流程圖/                  ← 共用參考（勿修改）
```

---

## 🔌 api.js ↔ ui.js 介面約定

`api.js` 負責提供以下**全域函式與變數**，`ui.js` 可以直接呼叫：

### 全域變數（由 api.js 暴露）

```javascript
// Firebase 資料庫物件
const db = firebase.database();
```

### 全域函式（由 api.js 提供）

```javascript
// 發送控制指令給 ESP32
// cmd: 字串，如 'M1_CW', 'FAN_ON', 'HOME', 'TEST_DISPENSE' 等
function sendCommand(cmd) { ... }

// 儲存設定到 Firebase
// alarmsArray: 字串陣列, 如 ['08:00', '12:30']
// targetCups: 整數, 0-5
function saveSettingsToFirebase(alarmsArray, targetCups) { ... }

// 從 Firebase 載入設定，透過回呼回傳
// callback(alarmsStr, targetCups): alarmsStr 如 "08:00,12:30"，targetCups 如 3
function loadSettingsFromFirebase(callback) { ... }
```

### 回呼函式（由 ui.js 提供，api.js 呼叫）

```javascript
// 當 Firebase 監控資料更新時呼叫
// data: Firebase /pillbox/monitor 節點的完整資料物件
function onMonitorUpdate(data) { ... }

// 當連線狀態變化時呼叫
// isOnline: boolean
function updateConnectionStatus(isOnline) { ... }
```

### HTML 載入順序（重要）

```html
<!-- index.html 中的載入順序 -->
<link rel="stylesheet" href="css/style.css">
<script src="https://www.gstatic.com/firebasejs/8.10.1/firebase-app.js"></script>
<script src="https://www.gstatic.com/firebasejs/8.10.1/firebase-database.js"></script>
<script src="js/api.js"></script>   <!-- 先載入：提供 sendCommand 等 -->
<script src="js/ui.js"></script>    <!-- 後載入：呼叫 api.js 的函式，實作 UI 回呼 -->
```

---

## 🏷️ HTML Element ID 約定

以下是 `api.js` 和 `ui.js` 共同約定的 HTML 元素 ID。**Antigravity 負責在 `index.html` 中建立這些元素，Claude 的 `api.js` 可以信賴這些 ID 存在。**

### 監控區域（ui.js 負責更新顯示）

| Element ID | 類型 | 用途 | 資料來源 |
|------------|------|------|----------|
| `loading` | `<p>` | 載入中提示 | — |
| `connection-status` | `<div>` | 連線狀態顯示 | `monitor/last_seen` |
| `temp-display` | `<div>` | 溫度顯示 | `monitor/temp` |
| `cup-0` ~ `cup-4` | `<div>` | 5 個藥杯指示燈 | `monitor/cups` |
| `hall-sensor` | `<div>` | 單點霍爾狀態 | `monitor/hall_sensor` |
| `lid-count` | `<div>` | 蓋子偵測數量 | `monitor/lid/count` |
| `lid-target` | `<div>` | 蓋子目標數量 | `monitor/lid/target` |
| `lid-match-msg` | `<div>` | 數量比對結果 | `monitor/lid/is_match` |
| `refill-status` | `<div>` | 補藥模式 banner | `monitor/refill_mode` |

### 設定區域（ui.js 讀取表單值，透過 api.js 函式存取 Firebase）

| Element ID | 類型 | 用途 |
|------------|------|------|
| `alarm-0` ~ `alarm-4` | `<input type="time">` | 5 組鬧鐘時間輸入 |
| `target-cups-input` | `<input type="number">` | 目標藥杯數輸入 |

### 按鈕（在 `index.html` 中直接呼叫 `sendCommand()` 或 UI 函式）

| 按鈕 `onclick` | 對應指令 |
|----------------|----------|
| `sendCommand('M1_CW')` | 圓盤正轉 |
| `sendCommand('M1_CCW')` | 圓盤反轉 |
| `sendCommand('M2_UP')` | 推桿上升 |
| `sendCommand('M2_DOWN')` | 推桿下降 |
| `sendCommand('FAN_ON')` / `sendCommand('FAN_OFF')` | 風扇開/關 |
| `sendCommand('LED_ON')` / `sendCommand('LED_OFF')` | 燈條開/關 |
| `sendCommand('PLAY_MUSIC')` | 播放音效 |
| `sendCommand('HOME')` | 回歸原點 |
| `sendCommand('TEST_DISPENSE')` | 出藥測試 |
| `sendCommand('DEMO_A')` / `sendCommand('DEMO_B')` | Demo 模式 |
| `saveSettings()` | 儲存設定（ui.js 中的包裝函式，內部呼叫 `saveSettingsToFirebase`） |

---

## ⚠️ 重要規則

### 🚫 不可以做的事

1. **不要修改對方負責的檔案**
   - Antigravity 不要動 `js/api.js` 和 `website_control/website_control.ino`
   - Claude 不要動 `index.html`、`css/style.css`、`js/ui.js`
2. **不要修改共用參考文件**（`規格書3.0.md`、`流程圖/`）
3. **不要改變約定好的介面**（函式名稱、參數格式），除非先和對方討論

### ✅ 可以做的事

1. **新增** HTML 元素的 `id` / `class`：Antigravity 可以新增，但需在 `COLLABORATION.md` 中記錄
2. **新增** 新的指令：Claude 可以在 `api.js` 和 `.ino` 新增新指令，但需在此文件記錄
3. **修改**自己負責的檔案中的任何內容

---

## 📡 Firebase 資料結構（共用參考）

```
/pillbox/
├── command          ← 網頁→ESP32 指令 (字串, 如 "M1_CW,1708012345678")
├── config/
│   ├── alarms_str   ← 鬧鐘設定 (字串, 如 "08:00,12:30,18:00")
│   └── target_cups  ← 目標藥杯數 (整數, 0-5)
└── monitor/
    ├── temp         ← 溫度 (浮點數)
    ├── cups         ← 藥杯狀態 (字串, 如 "1,0,1,0,0")
    ├── hall_sensor   ← 單點霍爾 (布林值)
    ├── last_seen    ← 心跳時間戳 (整數)
    ├── refill_mode  ← 補藥模式 (布林值)
    └── lid/
        ├── count    ← 目前偵測到的藥杯數 (整數)
        ├── target   ← 目標數量 (整數)
        └── is_match ← 是否符合 (布林值)
```

---

## 🔀 Git 工作流程

```bash
# 1. 確保 main 分支程式碼已 commit
git checkout main
git add -A && git commit -m "拆分 script.js 前的基準點"

# 2. Antigravity 在自己的分支工作
git checkout -b antigravity/web-ui

# 3. Claude 在自己的分支工作（從 main 分出）
git checkout main
git checkout -b claude/esp32-api

# 4. 完成後合併回 main
git checkout main
git merge antigravity/web-ui
git merge claude/esp32-api
```

---

## 📝 變更紀錄

在此記錄跨分工的介面變更：

| 日期 | 誰 | 變更內容 |
|------|-----|----------|
| 2026-02-20 | 共同 | 初始分工，拆分 `script.js` 為 `api.js` + `ui.js` |
