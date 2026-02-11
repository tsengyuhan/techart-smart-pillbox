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
            hallEl.classList.remove('hall-active');
        }
    }
});

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
