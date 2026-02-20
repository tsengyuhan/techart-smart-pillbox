// ==========================================
// api.js — Firebase 通訊層（Claude 負責）
// ==========================================
// 本檔案負責：Firebase 初始化、資料監聽、指令發送
// 不直接操作 DOM，透過回呼函式與 ui.js 溝通
// 參見 COLLABORATION.md 了解介面約定

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

// --- 心跳追蹤 ---
let lastHeartbeatTime = 0;

// --- 監聽 Firebase 資料變化 ---
const monitorRef = db.ref('/pillbox/monitor');

monitorRef.on('value', (snapshot) => {
    const data = snapshot.val();

    if (data) {
        if (data.last_seen) {
            lastHeartbeatTime = Date.now();
            // 通知 ui.js 連線正常
            if (typeof updateConnectionStatus === 'function') {
                updateConnectionStatus(true);
            }
        }

        // 通知 ui.js 更新畫面
        if (typeof onMonitorUpdate === 'function') {
            onMonitorUpdate(data);
        }
    }
});

// --- 斷線偵測（每秒檢查，超過 6 秒無心跳即判定斷線）---
setInterval(() => {
    const now = Date.now();
    if (now - lastHeartbeatTime > 6000) {
        if (typeof updateConnectionStatus === 'function') {
            updateConnectionStatus(false);
        }
    }
}, 1000);

// --- 發送控制指令 ---
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

// --- 儲存設定到 Firebase ---
// alarmsArray: 字串陣列, 如 ['08:00', '12:30']
// targetCups: 整數, 0-5
function saveSettingsToFirebase(alarmsArray, targetCups) {
    const alarmsStr = alarmsArray.join(',');

    const updates = {};
    updates['/pillbox/config/alarms_str'] = alarmsStr;
    updates['/pillbox/config/target_cups'] = targetCups;

    return db.ref().update(updates);
}

// --- 從 Firebase 載入設定 ---
// callback(alarmsStr, targetCups)
function loadSettingsFromFirebase(callback) {
    Promise.all([
        db.ref('/pillbox/config/alarms_str').once('value'),
        db.ref('/pillbox/config/target_cups').once('value')
    ]).then(([alarmsSnap, cupsSnap]) => {
        const alarmsStr = alarmsSnap.val() || '';
        const targetCups = cupsSnap.val() || 0;
        if (typeof callback === 'function') {
            callback(alarmsStr, targetCups);
        }
    });
}
