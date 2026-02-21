// ==========================================
// ui.js — UI 互動層（Antigravity 負責）
// ==========================================
// 本檔案負責：DOM 操作、畫面更新、動畫效果、SPA 切換
// 透過 api.js 提供的函式與 Firebase 溝通

// --- SPA 頁面切換邏輯 ---
function switchPage(pageId, title) {
    // 隱藏所有頁面
    document.querySelectorAll('.page').forEach(page => {
        page.classList.remove('active');
    });
    // 顯示指定頁面
    document.getElementById('page-' + pageId).classList.add('active');

    // 更新標題
    document.getElementById('page-title').innerText = title;

    // 更新底部導覽列狀態
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.remove('active');
    });
    // 找尋對應的 nav-item (用 onclick 內容簡易判斷)
    const activeNav = Array.from(document.querySelectorAll('.nav-item')).find(el => el.getAttribute('onclick').includes(pageId));
    if (activeNav) activeNav.classList.add('active');
}

// --- 開發者密碼鎖 ---
function promptDevLogin() {
    document.getElementById('password-dialog').style.display = 'flex';
    document.getElementById('dev-password').value = '';
    document.getElementById('dev-password').focus();
}

function closePasswordDialog() {
    document.getElementById('password-dialog').style.display = 'none';
}

function checkPassword() {
    const pwd = document.getElementById('dev-password').value;
    if (pwd === '1234') {
        closePasswordDialog();
        switchPage('manual', '⚙️ 手動控制');
    } else {
        alert('密碼錯誤！');
        document.getElementById('dev-password').value = '';
    }
}

// --- 補藥流程 (Refill Wizard) ---
function openRefillWizard() {
    document.getElementById('refill-wizard').style.display = 'flex';
    // 通知 ESP32 機器歸零並進入補藥模式
    if (typeof sendCommand === 'function') {
        sendCommand('ENTER_REFILL');
        // 播放提示音 (透過 ESP32 的指令，這裡假設使用 PLAY_MUSIC 或新增一個專用音效指令)
        sendCommand('PLAY_MUSIC');
    }
}

function cancelRefillWizard() {
    document.getElementById('refill-wizard').style.display = 'none';
    if (typeof sendCommand === 'function') sendCommand('EXIT_REFILL');
}

function finishRefillWizard() {
    document.getElementById('refill-wizard').style.display = 'none';
    if (typeof sendCommand === 'function') sendCommand('EXIT_REFILL'); // 退出並封孔
    alert('✅ 補藥完成！機器已重新待機。');
}

// --- 計算並顯示下一次出藥時間 ---
function updateNextAlarmDisplay(alarmsStr) {
    if (!alarmsStr) {
        document.getElementById('next-alarm-display').innerText = '--:--';
        return;
    }

    const times = alarmsStr.split(',').map(t => t.trim()).filter(t => t);
    if (times.length === 0) {
        document.getElementById('next-alarm-display').innerText = '--:--';
        return;
    }

    // 取得現在時間 (時:分)
    const now = new Date();
    const currentMins = now.getHours() * 60 + now.getMinutes();

    // 尋找今天接下來的最近一個鬧鐘
    let nextAlarm = null;
    let minDiff = Infinity;

    // 將鬧鐘時間轉為分鐘數比較
    const alarmsInMins = times.map(timeStr => {
        const [h, m] = timeStr.split(':').map(Number);
        return { timeStr, mins: h * 60 + m };
    });

    // 排序鬧鐘 (確保照順序)
    alarmsInMins.sort((a, b) => a.mins - b.mins);

    for (let alarm of alarmsInMins) {
        if (alarm.mins > currentMins) {
            nextAlarm = alarm.timeStr;
            break;
        }
    }

    // 如果今天都沒鬧鐘了，顯示明天的第一個
    if (!nextAlarm) nextAlarm = '明天 ' + alarmsInMins[0].timeStr;

    document.getElementById('next-alarm-display').innerText = nextAlarm;
}

// --- 回呼函式：Firebase 監控資料更新時由 api.js 呼叫 ---
function onMonitorUpdate(data) {
    // document.getElementById('loading').style.display = 'none'; // DOM 裡已隱藏

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
        hallEl.innerText = "🚨 霍爾：偵測到磁鐵！";
        hallEl.classList.add('hall-active');
        hallEl.classList.remove('hall-inactive');
    } else {
        hallEl.innerText = "取藥偵測：無訊號";
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
            msgEl.style.color = "var(--primary-dark)";
        } else {
            msgEl.innerText = "⚠️ 數量不符";
            msgEl.style.color = "var(--danger)";
        }
    }

    // 更新補藥模式狀態 (Banner)
    const refillBanner = document.getElementById('refill-status');
    const refillWizardBtn = document.querySelector('.btn-refill-huge');

    if (data.refill_mode === true) {
        refillBanner.style.display = 'block';
        if (refillWizardBtn) refillWizardBtn.disabled = true; // 鎖定按鈕避免重複按
    } else {
        refillBanner.style.display = 'none';
        if (refillWizardBtn) refillWizardBtn.disabled = false;
    }
}

// --- 回呼函式：連線狀態變化時由 api.js 呼叫 ---
function updateConnectionStatus(isOnline) {
    const connectionStatusEl = document.getElementById('connection-status');
    if (isOnline) {
        connectionStatusEl.innerText = "🟢 已連線";
        connectionStatusEl.className = "status-badge online";
    } else {
        connectionStatusEl.innerText = "🔴 離線 (ESP32無回應)";
        connectionStatusEl.className = "status-badge offline";
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
    if (typeof saveSettingsToFirebase === 'function') {
        saveSettingsToFirebase(times, targetCups)
            .then(() => {
                alert('✅ 設定已儲存！');
                updateNextAlarmDisplay(times.join(',')); // 立即更新顯示
            })
            .catch((error) => {
                alert('❌ 儲存失敗: ' + error.message);
            });
    } else {
        alert('警告：無法連線到資料庫 (api.js 未載入)');
    }
}

// --- 載入設定（透過 api.js 讀取後填入表單）---
function loadSettings() {
    if (typeof loadSettingsFromFirebase === 'function') {
        loadSettingsFromFirebase((alarmsStr, targetCups) => {
            // 填入鬧鐘
            if (alarmsStr) {
                const times = alarmsStr.split(',');
                times.forEach((time, index) => {
                    const el = document.getElementById('alarm-' + index);
                    if (el) el.value = time.trim();
                });

                // 初次載入時更新「下一次出藥時間」
                updateNextAlarmDisplay(alarmsStr);
            }

            // 填入目標數量
            if (targetCups !== null) {
                document.getElementById('target-cups-input').value = targetCups;
            }
        });
    }
}

// --- 載入歷史紀錄模擬 (尚未與 Firebase logs/ 串接) ---
function loadMoreHistory() {
    // 此處未來由 api.js 負責從 /pillbox/logs 撈取
    alert("歷史紀錄功能即將上線！");
}

// --- 頁面載入時執行 ---
document.addEventListener('DOMContentLoaded', () => {
    loadSettings();

    // 每分鐘更新一次「下一次出藥時間」的顯示
    setInterval(() => {
        if (typeof loadSettingsFromFirebase === 'function') {
            loadSettingsFromFirebase((alarmsStr, targetCups) => {
                updateNextAlarmDisplay(alarmsStr);
            });
        }
    }, 60000);
});

