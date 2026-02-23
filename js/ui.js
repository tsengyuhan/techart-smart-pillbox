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

    // 動態產生補藥清單
    const stepsList = document.getElementById('refill-steps-list');
    if (stepsList) {
        stepsList.innerHTML = ''; // 清空舊的
        const times = [];
        for (let i = 0; i < 5; i++) {
            const val = document.getElementById('alarm-' + i).value;
            if (val) times.push(val);
        }

        // 排序時間
        times.sort((a, b) => {
            const [h1, m1] = a.split(':').map(Number);
            const [h2, m2] = b.split(':').map(Number);
            return (h1 * 60 + m1) - (h2 * 60 + m2);
        });

        if (times.length === 0) {
            stepsList.innerHTML = '<li><span class="cup-num">⚠️ 尚未設定任何出藥時間</span></li>';
        } else {
            times.forEach((time, index) => {
                const li = document.createElement('li');
                li.innerHTML = `<span class="cup-num">第 ${index + 1} 杯</span> <span>放入 ${time} 的藥物</span>`;
                stepsList.appendChild(li);
            });
        }
    }

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

// --- 全域警告系統 (Global Alerts) ---
let currentAlertCode = null;

function showGlobalAlert(message, showConfirmBtn = false, alertCode = null) {
    const banner = document.getElementById('global-alert-banner');
    const msgEl = document.getElementById('alert-message');
    const confirmBtn = document.getElementById('alert-confirm-btn');
    const dismissBtn = document.getElementById('alert-dismiss-btn');

    if (banner && msgEl) {
        msgEl.innerText = message;
        currentAlertCode = alertCode;

        if (showConfirmBtn) {
            confirmBtn.style.display = 'inline-block';
            dismissBtn.style.display = 'none'; // 強制確認不允許單純關閉
        } else {
            confirmBtn.style.display = 'none';
            dismissBtn.style.display = 'inline-block';
        }

        banner.style.display = 'flex';
    }
}

function dismissAlert() {
    const banner = document.getElementById('global-alert-banner');
    if (banner) banner.style.display = 'none';
    currentAlertCode = null;
}

function confirmHardwareAlert() {
    // 傳送硬體解鎖指令
    if (typeof sendCommand === 'function') {
        // 根據不同錯誤發送不同的對應指令 (預設送 CLEAR_ERROR)
        const cmd = currentAlertCode === 'pusher_stuck' ? 'CLEAR_PUSHER_ERROR' : 'CLEAR_ERROR';
        sendCommand(cmd);
    }
    dismissAlert();
}

// --- 回呼函式：Firebase 監控資料更新時由 api.js 呼叫 ---
function onMonitorUpdate(data) {
    // 處理全域警告 (Error States from ESP32)
    if (data.error_state) {
        if (data.error_state === 'pusher_stuck') {
            showGlobalAlert("⚠️ 請確認推桿是否在圓盤下方，並點擊確認", true, 'pusher_stuck');
        } else if (data.error_state === 'lid_error') {
            showGlobalAlert("🚨 蓋子狀態異常！(開啟超過1分鐘)");
        } else if (data.error_state === 'cup_not_taken') {
            // 對應流程圖：超過3分鐘忘了吃藥，強制回收
            showGlobalAlert(`🚨 強制回收：位置 ${data.last_active_cup || '?'} 藥杯未被取走！`);
        } else if (data.error_state === 'refill_cups_left') {
            showGlobalAlert("⚠️ 補藥完成，但偵測到尚有空杯未收走！");
        } else if (data.error_state === 'previous_cup_left') {
            showGlobalAlert("⚠️ 此藥杯上次未取走，仍繼續出藥");
        }
    } else {
        // 沒有錯誤就確保 Banner 關閉 (除非使用者還沒按確認)
        const banner = document.getElementById('global-alert-banner');
        const confirmBtn = document.getElementById('alert-confirm-btn');
        if (banner && confirmBtn && confirmBtn.style.display === 'none') {
            dismissAlert();
        }
    }

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

// --- 自動計算藥杯總數 ---
function updateTargetCupsCount() {
    let count = 0;
    for (let i = 0; i < 5; i++) {
        const val = document.getElementById('alarm-' + i).value;
        if (val) count++;
    }
    const displayEl = document.getElementById('target-cups-display');
    const inputEl = document.getElementById('target-cups-input');
    if (displayEl) displayEl.innerText = count;
    if (inputEl) inputEl.value = count;
}

// --- 儲存設定（UI 包裝函式，讀取表單後呼叫 api.js）---
function saveSettings() {
    // 讀取鬧鐘時間
    const times = [];
    for (let i = 0; i < 5; i++) {
        const val = document.getElementById('alarm-' + i).value;
        if (val) times.push(val);
    }

    // 重新計算目標數量 (確保與畫面一致)
    updateTargetCupsCount();
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

            // 更新目標數量 (自動計算)
            updateTargetCupsCount();
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

    // 綁定鬧鐘輸入的 change 以及 input 事件來自動計算藥杯數
    for (let i = 0; i < 5; i++) {
        const el = document.getElementById('alarm-' + i);
        if (el) {
            el.addEventListener('change', updateTargetCupsCount);
            el.addEventListener('input', updateTargetCupsCount);
        }
    }

    // 每分鐘更新一次「下一次出藥時間」的顯示
    setInterval(() => {
        if (typeof loadSettingsFromFirebase === 'function') {
            loadSettingsFromFirebase((alarmsStr, targetCups) => {
                updateNextAlarmDisplay(alarmsStr);
            });
        }
    }, 60000);
});

