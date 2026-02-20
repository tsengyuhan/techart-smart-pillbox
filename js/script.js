// Firebase 設定
const firebaseConfig = {
    apiKey: "AIzaSyBbp0kENACTRcVmV2PZW8Q2pHNtMdGhbZ0",
    authDomain: "smart-pillbox-23113.firebaseapp.com",
    databaseURL: "https://smart-pillbox-23113-default-rtdb.firebaseio.com",
    projectId: "smart-pillbox-23113",
    storageBucket: "smart-pillbox-23113.firebasestorage.app",
    messagingSenderId: "228363023113",
    appId: "1:228363023113:web:1522c20f1e29f2029499d8"
};

// 初始化 Firebase
if (!firebase.apps.length) {
    firebase.initializeApp(firebaseConfig);
}
const db = firebase.database();

let lastHeartbeatTime = 0;
const connectionStatusEl = document.getElementById('connection-status');

// 監聽 Firebase 資料變化
const monitorRef = db.ref('/pillbox/monitor');

monitorRef.on('value', (snapshot) => {
    document.getElementById('loading').style.display = 'none';
    const data = snapshot.val();

    if (data) {
        if (data.last_seen) {
            lastHeartbeatTime = Date.now();
            updateConnectionStatus(true);
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
            hallEl.innerText = "🚨 單點霍爾：偵測到磁鐵！";
            hallEl.classList.add('hall-active');
            hallEl.classList.remove('hall-inactive');
        } else {
            hallEl.innerText = "單點霍爾：無訊號";
            hallEl.classList.add('hall-inactive');
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
});

// 載入設定
function loadSettings() {
    // 載入鬧鐘
    db.ref('/pillbox/config/alarms_str').once('value').then((snapshot) => {
        const raw = snapshot.val();
        if (raw) {
            const times = raw.split(',');
            times.forEach((time, index) => {
                const el = document.getElementById('alarm-' + index);
                if (el) el.value = time.trim();
            });
        }
    });

    // 載入目標數量
    db.ref('/pillbox/config/target_cups').once('value').then((snapshot) => {
        const val = snapshot.val();
        if (val !== null) {
            document.getElementById('target-cups-input').value = val;
        }
    });
}
// 頁面載入時執行
loadSettings();

// 儲存設定
function saveSettings() {
    // 1. 儲存鬧鐘
    const times = [];
    for (let i = 0; i < 5; i++) {
        const val = document.getElementById('alarm-' + i).value;
        if (val) times.push(val);
    }
    const alarmsStr = times.join(',');

    // 2. 儲存目標數量
    const targetCups = parseInt(document.getElementById('target-cups-input').value) || 0;

    // 寫入 Firebase
    const updates = {};
    updates['/pillbox/config/alarms_str'] = alarmsStr;
    updates['/pillbox/config/target_cups'] = targetCups;

    db.ref().update(updates)
        .then(() => {
            alert('✅ 設定已儲存！');
        })
        .catch((error) => {
            alert('❌ 儲存失敗: ' + error.message);
        });
}

// 斷線偵測（每秒檢查，超過 6 秒無心跳即判定斷線）
setInterval(() => {
    const now = Date.now();
    if (now - lastHeartbeatTime > 6000) {
        updateConnectionStatus(false);
    }
}, 1000);

function updateConnectionStatus(isOnline) {
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

// 發送控制指令
function sendCommand(cmd) {
    const commandWithId = cmd + "," + Date.now();

    db.ref('/pillbox/command').set(commandWithId)
        .then(() => {
            console.log("指令發送成功:", commandWithId);
        })
        .catch((error) => {
            alert("發送失敗: " + error.message);
        });
}
