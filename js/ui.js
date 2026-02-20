// ==========================================
// ui.js — UI 互動層（Antigravity 負責）
// ==========================================
// 本檔案負責：DOM 操作、畫面更新、動畫效果
// 透過 api.js 提供的函式與 Firebase 溝通
// 參見 COLLABORATION.md 了解介面約定

// --- 回呼函式：Firebase 監控資料更新時由 api.js 呼叫 ---
function onMonitorUpdate(data) {
    document.getElementById('loading').style.display = 'none';

    // 更新溫度
    if (data.temp) {
        document.getElementById('temp-display').innerText = data.temp + " °C";
    }

    // 更新藥杯
    if (data.cups) {
        const cupStates = data.cups.split(',');
        cupStates.forEach((state, index) => {
            const cupEl = document.getElementById('cup-' + index);
            if (state === '1') {
                cupEl.classList.add('active');
                cupEl.classList.remove('inactive');
            } else {
                cupEl.classList.add('inactive');
                cupEl.classList.remove('active');
            }
        });
    }

    // 更新霍爾
    const hallEl = document.getElementById('hall-sensor');
    if (data.hall_sensor === true) {
        hallEl.innerText = "🚨 單點霍爾：偵測到磁鐵！";
        hallEl.classList.add('hall-active');
        hallEl.classList.remove('hall-inactive');
    } else {
        hallEl.innerText = "單點霍爾：無訊號";
        hallEl.classList.add('hall-inactive');
        hallEl.classList.remove('hall-active');
    }

    // 更新蓋子藥杯資訊
    if (data.lid) {
        document.getElementById('lid-count').innerText = (data.lid.count !== undefined) ? data.lid.count : "--";
        document.getElementById('lid-target').innerText = (data.lid.target !== undefined) ? data.lid.target : "--";

        const msgEl = document.getElementById('lid-match-msg');
        if (data.lid.is_match) {
            msgEl.innerText = "✅ 數量符合";
            msgEl.style.color = "green";
        } else {
            msgEl.innerText = "⚠️ 數量不符";
            msgEl.style.color = "orange";
        }
    }

    // 更新補藥模式狀態
    const refillBanner = document.getElementById('refill-status');
    if (data.refill_mode === true) {
        refillBanner.style.display = 'block';
    } else {
        refillBanner.style.display = 'none';
    }
}

// --- 回呼函式：連線狀態變化時由 api.js 呼叫 ---
function updateConnectionStatus(isOnline) {
    const connectionStatusEl = document.getElementById('connection-status');
    if (isOnline) {
        connectionStatusEl.innerText = "🟢 已連線";
        connectionStatusEl.style.color = "#2e7d32";
        connectionStatusEl.style.fontWeight = "bold";
    } else {
        connectionStatusEl.innerText = "🔴 已斷線 (ESP32 無回應)";
        connectionStatusEl.style.color = "red";
        connectionStatusEl.style.fontWeight = "bold";
    }
}

// --- 儲存設定（UI 包裝函式，讀取表單後呼叫 api.js）---
function saveSettings() {
    // 讀取鬧鐘時間
    const times = [];
    for (let i = 0; i < 5; i++) {
        const val = document.getElementById('alarm-' + i).value;
        if (val) times.push(val);
    }

    // 讀取目標數量
    const targetCups = parseInt(document.getElementById('target-cups-input').value) || 0;

    // 透過 api.js 寫入 Firebase
    saveSettingsToFirebase(times, targetCups)
        .then(() => {
            alert('✅ 設定已儲存！');
        })
        .catch((error) => {
            alert('❌ 儲存失敗: ' + error.message);
        });
}

// --- 載入設定（透過 api.js 讀取後填入表單）---
function loadSettings() {
    loadSettingsFromFirebase((alarmsStr, targetCups) => {
        // 填入鬧鐘
        if (alarmsStr) {
            const times = alarmsStr.split(',');
            times.forEach((time, index) => {
                const el = document.getElementById('alarm-' + index);
                if (el) el.value = time.trim();
            });
        }

        // 填入目標數量
        if (targetCups !== null) {
            document.getElementById('target-cups-input').value = targetCups;
        }
    });
}

// --- 頁面載入時執行 ---
loadSettings();
