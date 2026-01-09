# Guardian Eye - 遠端控制系統設計文件
## Node.js 網頁伺服器 + Discord Bot

> **設計目標：** 讓手機可以透過網頁或 Discord 進行遠端解鎖  
> **通訊方式：** 檔案共享（Qt ↔ Node.js ↔ Discord）  
> **網路方案：** 同一 WiFi 區域網路

---

## 📁 專案結構

```
guardian_eye_remote/
├── server.js              # Node.js 主伺服器（唯一的 JS 程式碼）
├── discord_bot.js         # Discord Bot（選用，加分項目）
├── public/                # 靜態網頁檔案
│   ├── index.html         # 主頁面（不需要寫）
│   ├── style.css          # 樣式（不需要寫）
│   └── app.js             # 前端 JS（不需要寫）
└── README.md              # 使用說明
```

---

## 🚀 快速啟動指南

### 初始化專案

*注意*：因爲是在Nvidia TX2上執行，所以不需要這一步
```bash
# 初始化 package.json
npm init -y

# 安裝依賴
npm install express
npm install discord.js  # 如果要做 Discord Bot
```

---

## 📄 package.json 配置

```json
{
  "name": "guardian-eye-remote",
  "version": "1.0.0",
  "description": "Guardian Eye Remote Control System",
  "main": "server.js",
  "scripts": {
    "start": "node server.js",
    "dev": "node server.js",
    "discord": "node discord_bot.js"
  },
  "keywords": ["guardian-eye", "security", "iot"],
  "author": "NTUT Group XX",
  "license": "MIT",
  "dependencies": {
    "express": "^4.18.2",
    "discord.js": "^14.14.1"
  }
}
```

---

## 🌐 Node.js 網頁伺服器

### server.js（完整程式碼）

```javascript
// server.js
const express = require("express");
const fs = require("fs");
const path = require("path");

const app = express();
const PORT = 8080;

// 中介軟體
app.use(express.json());
app.use(express.static("public"));  // 提供靜態檔案（HTML/CSS/JS）

// ========================= 共享檔案路徑 =========================
const ALARM_STATUS_FILE = "/tmp/guardian_alarm_status.json";
const UNLOCK_STATUS_FILE = "/tmp/guardian_unlock_status.json";
const CONTROL_FILE = "/tmp/guardian_control.txt";

// 預設密碼（可以改成從環境變數讀取）
const CORRECT_PASSWORD = process.env.UNLOCK_PASSWORD || "1234";

// ========================= 工具函數 =========================

/**
 * 讀取警報狀態（Qt 寫入，Node.js 讀取）
 */
function readAlarmStatus() {
  try {
    if (fs.existsSync(ALARM_STATUS_FILE)) {
      const data = fs.readFileSync(ALARM_STATUS_FILE, "utf8");
      return JSON.parse(data);
    }
  } catch (error) {
    console.error("讀取警報狀態失敗:", error.message);
  }
  
  // 預設值
  return {
    alarm_active: false,
    alarm_type: null,       // 'pig', 'stranger', null
    timestamp: null,
    image_path: null,
    confidence: 0
  };
}

/**
 * 讀取解鎖狀態（Qt 讀取，Node.js 寫入）
 */
function readUnlockStatus() {
  try {
    if (fs.existsSync(UNLOCK_STATUS_FILE)) {
      const data = fs.readFileSync(UNLOCK_STATUS_FILE, "utf8");
      return JSON.parse(data);
    }
  } catch (error) {
    console.error("讀取解鎖狀態失敗:", error.message);
  }
  
  return {
    remote_unlocked: false,
    password_correct: false,
    timestamp: null,
    random_code: null
  };
}

/**
 * 寫入解鎖狀態（供 Qt 輪詢）
 */
function writeUnlockStatus(data) {
  try {
    fs.writeFileSync(UNLOCK_STATUS_FILE, JSON.stringify(data, null, 2));
    console.log("✅ 解鎖狀態已更新:", data);
  } catch (error) {
    console.error("寫入解鎖狀態失敗:", error.message);
  }
}

/**
 * 讀取系統日誌（從 Kernel Driver 或檔案）
 */
function readSystemLogs() {
  // 這裡可以從 Kernel Driver 讀取，或從日誌檔讀取
  // 簡化版：從檔案讀取
  const LOG_FILE = "/tmp/guardian_logs.json";
  
  try {
    if (fs.existsSync(LOG_FILE)) {
      const data = fs.readFileSync(LOG_FILE, "utf8");
      return JSON.parse(data);
    }
  } catch (error) {
    console.error("讀取日誌失敗:", error.message);
  }
  
  // 預設假資料（展示用）
  return [
    {
      time: "2025-01-09 14:25:05",
      event: "小豬入侵警報",
      status: "已解除",
      type: "alarm"
    },
    {
      time: "2025-01-09 10:15:32",
      event: "陌生人偵測",
      status: "已通知",
      type: "detection"
    },
    {
      time: "2025-01-09 08:30:15",
      event: "主人開門",
      status: "正常",
      type: "access"
    }
  ];
}

// ========================= API 路由 =========================

/**
 * GET /api/status
 * 功能：取得當前系統狀態（警報、解鎖狀態）
 * 前端會每秒輪詢這個 API
 */
app.get("/api/status", (req, res) => {
  const alarmStatus = readAlarmStatus();
  const unlockStatus = readUnlockStatus();
  
  res.json({
    // 警報狀態
    alarm_active: alarmStatus.alarm_active,
    alarm_type: alarmStatus.alarm_type,
    alarm_time: alarmStatus.timestamp,
    alarm_confidence: alarmStatus.confidence,
    
    // 解鎖狀態
    remote_unlocked: unlockStatus.remote_unlocked,
    awaiting_local_code: unlockStatus.remote_unlocked && !unlockStatus.password_correct,
    
    // 系統資訊
    server_time: new Date().toISOString(),
    uptime: process.uptime()
  });
});

/**
 * POST /api/unlock
 * 功能：遠端解鎖（驗證密碼）
 * 請求體：{ "password": "1234" }
 */
app.post("/api/unlock", (req, res) => {
  const { password } = req.body;
  
  // 驗證密碼
  if (!password) {
    return res.status(400).json({
      success: false,
      message: "❌ 請輸入密碼"
    });
  }
  
  if (password === CORRECT_PASSWORD) {
    // 密碼正確，寫入解鎖狀態
    const unlockData = {
      remote_unlocked: true,
      password_correct: true,
      timestamp: new Date().toISOString(),
      unlock_method: "web"
    };
    
    writeUnlockStatus(unlockData);
    
    // 記錄到日誌
    logEvent("遠端解鎖成功", "unlock", "success");
    
    res.json({
      success: true,
      message: "✅ 遠端驗證通過！請回家輸入現場隨機密碼。"
    });
  } else {
    // 密碼錯誤
    logEvent("遠端解鎖失敗（密碼錯誤）", "unlock", "failed");
    
    res.status(401).json({
      success: false,
      message: "❌ 密碼錯誤，請重試。"
    });
  }
});

/**
 * GET /api/history
 * 功能：取得歷史記錄
 */
app.get("/api/history", (req, res) => {
  const logs = readSystemLogs();
  res.json(logs);
});

/**
 * POST /api/control
 * 功能：手動控制（如：強制開門、靜音警報）
 * 請求體：{ "action": "open_door" | "mute_alarm" | "reset" }
 */
app.post("/api/control", (req, res) => {
  const { action } = req.body;
  
  const validActions = ["open_door", "mute_alarm", "reset", "test_alarm"];
  
  if (!validActions.includes(action)) {
    return res.status(400).json({
      success: false,
      message: "❌ 無效的控制指令"
    });
  }
  
  // 寫入控制檔（Qt 輪詢讀取）
  try {
    fs.writeFileSync(CONTROL_FILE, action);
    console.log(`✅ 控制指令已發送: ${action}`);
    
    res.json({
      success: true,
      action: action,
      message: `✅ 已執行: ${action}`
    });
  } catch (error) {
    res.status(500).json({
      success: false,
      message: "❌ 控制指令發送失敗"
    });
  }
});

/**
 * GET /api/image/:filename
 * 功能：取得警報時拍攝的照片
 */
app.get("/api/image/:filename", (req, res) => {
  const filename = req.params.filename;
  const imagePath = path.join("/tmp/guardian_images", filename);
  
  if (fs.existsSync(imagePath)) {
    res.sendFile(imagePath);
  } else {
    res.status(404).send("圖片不存在");
  }
});

/**
 * POST /api/reset-unlock
 * 功能：重置解鎖狀態（測試用）
 */
app.post("/api/reset-unlock", (req, res) => {
  writeUnlockStatus({
    remote_unlocked: false,
    password_correct: false,
    timestamp: null
  });
  
  res.json({
    success: true,
    message: "✅ 解鎖狀態已重置"
  });
});

// ========================= 日誌記錄 =========================

function logEvent(eventName, eventType, status) {
  const LOG_FILE = "/tmp/guardian_logs.json";
  
  const newLog = {
    time: new Date().toLocaleString("zh-TW"),
    event: eventName,
    status: status,
    type: eventType
  };
  
  try {
    let logs = [];
    if (fs.existsSync(LOG_FILE)) {
      const data = fs.readFileSync(LOG_FILE, "utf8");
      logs = JSON.parse(data);
    }
    
    // 新增到最前面（最新的在上面）
    logs.unshift(newLog);
    
    // 只保留最近 50 筆
    if (logs.length > 50) {
      logs = logs.slice(0, 50);
    }
    
    fs.writeFileSync(LOG_FILE, JSON.stringify(logs, null, 2));
  } catch (error) {
    console.error("寫入日誌失敗:", error.message);
  }
}

// ========================= 健康檢查 =========================

app.get("/health", (req, res) => {
  res.json({
    status: "healthy",
    uptime: process.uptime(),
    timestamp: new Date().toISOString()
  });
});

// ========================= 啟動伺服器 =========================

app.listen(PORT, "0.0.0.0", () => {
  console.log("========================================");
  console.log("🛡️  Guardian Eye Remote Control Server");
  console.log("========================================");
  console.log(`✅ Server running on http://0.0.0.0:${PORT}`);
  console.log(`📱 手機訪問（請確保在同一 WiFi）：`);
  
  // 嘗試取得本機 IP
  const os = require("os");
  const interfaces = os.networkInterfaces();
  
  Object.keys(interfaces).forEach((ifname) => {
    interfaces[ifname].forEach((iface) => {
      if (iface.family === "IPv4" && !iface.internal) {
        console.log(`   http://${iface.address}:${PORT}`);
      }
    });
  });
  
  console.log("========================================");
  console.log(`📂 靜態檔案目錄: ${path.join(__dirname, "public")}`);
  console.log(`🔐 解鎖密碼: ${CORRECT_PASSWORD}`);
  console.log("========================================");
});

// 優雅關閉
process.on("SIGINT", () => {
  console.log("\n\n👋 伺服器正在關閉...");
  process.exit(0);
});
```

---

## 🤖 Discord Bot（選用，架構說明）

### discord_bot.js 功能設計

**核心功能：**
1. 監聽 Discord 頻道訊息
2. 使用者輸入密碼時驗證並更新解鎖狀態
3. 主動推播警報訊息（Qt 觸發時透過檔案通知）
4. 附加照片（陌生人照片）

**技術架構：**
- 使用 discord.js v14
- Token 從 Discord Developer Portal 取得
- 透過檔案與 Qt 通訊

**通訊方式：**
- Qt 寫入警報 → `/tmp/guardian_discord_queue.json`
- Discord Bot 讀取 → 發送訊息 → 刪除檔案
- 使用者在 Discord 輸入密碼 → Bot 寫入解鎖狀態 → Qt 輪詢讀取

**實作重點：**
- 每秒輪詢檔案檢查新警報
- 驗證使用者身份（只回應特定頻道）
- 支援附加圖片（使用 AttachmentBuilder）

---

## 📡 Qt 與 Node.js 通訊協議

### 共享檔案格式

#### 1. 警報狀態（Qt 寫入，Node.js 讀取）

**檔案：** `/tmp/guardian_alarm_status.json`

```json
{
  "alarm_active": true,
  "alarm_type": "pig",
  "timestamp": "2025-01-09 14:25:05",
  "image_path": "/tmp/guardian_images/pig_001.jpg",
  "confidence": 0.96
}
```

**欄位說明：**
- `alarm_active`：是否有警報（true/false）
- `alarm_type`：警報類型（"pig", "stranger", null）
- `timestamp`：觸發時間
- `image_path`：照片路徑（可選）
- `confidence`：AI 信心度（0-1）

---

#### 2. 解鎖狀態（Node.js 寫入，Qt 讀取）

**檔案：** `/tmp/guardian_unlock_status.json`

```json
{
  "remote_unlocked": true,
  "password_correct": true,
  "timestamp": "2025-01-09 14:26:00",
  "unlock_method": "web"
}
```

**欄位說明：**
- `remote_unlocked`：遠端是否已解鎖（true/false）
- `password_correct`：密碼是否正確（true/false）
- `timestamp`：解鎖時間
- `unlock_method`：解鎖方式（"web", "discord"）

---

#### 3. 控制指令（Node.js 寫入，Qt 讀取）

**檔案：** `/tmp/guardian_control.txt`

**內容：** 單行文字（指令名稱）

```
open_door
```

**可用指令：**
- `open_door`：強制開門
- `mute_alarm`：靜音警報
- `reset`：重置系統
- `test_alarm`：測試警報

**Qt 處理流程：**
1. 每秒檢查檔案是否存在
2. 讀取指令
3. 執行對應動作
4. 刪除檔案（表示已處理）

---

#### 4. Discord 推播佇列（Qt 寫入，Discord Bot 讀取）

**檔案：** `/tmp/guardian_discord_queue.json`

```json
{
  "type": "pig_intrusion",
  "message": "🚨 偵測到小豬玩偶入侵！",
  "timestamp": "2025-01-09 14:25:05",
  "image_path": "/tmp/guardian_images/intrusion_001.jpg",
  "priority": "high"
}
```

**Discord Bot 處理流程：**
1. 每秒檢查檔案是否存在
2. 讀取內容並發送到 Discord 頻道
3. 如果有 `image_path`，附加圖片
4. 刪除檔案

---

## 🔄 完整流程圖

### 警報觸發 → 遠端解鎖流程

```
1. AI 偵測到小豬
   ↓
2. Qt 寫入警報狀態
   /tmp/guardian_alarm_status.json
   {alarm_active: true, alarm_type: "pig"}
   ↓
3. Qt 寫入 Discord 佇列（選用）
   /tmp/guardian_discord_queue.json
   ↓
4. 手機瀏覽器輪詢 /api/status
   發現 alarm_active = true
   顯示警報訊息
   ↓
5. 使用者在手機輸入密碼 "1234"
   POST /api/unlock
   ↓
6. Node.js 驗證密碼
   寫入 /tmp/guardian_unlock_status.json
   {remote_unlocked: true, password_correct: true}
   ↓
7. Qt 輪詢發現 remote_unlocked = true
   顯示隨機碼 "739"
   ↓
8. 使用者在 Qt 介面輸入 "739"
   ↓
9. Qt 驗證成功
   關閉警報、寫入 Kernel Log
   更新狀態檔
   {alarm_active: false}
   ↓
10. 手機瀏覽器顯示「系統正常」
```

---

## 🛠️ Qt 端整合（Python 範例）

### 寫入警報狀態

```python
import json
from datetime import datetime

def trigger_alarm(alarm_type='pig', image_path=None, confidence=0.0):
    """觸發警報"""
    status = {
        'alarm_active': True,
        'alarm_type': alarm_type,
        'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
        'image_path': image_path,
        'confidence': confidence
    }
    
    with open('/tmp/guardian_alarm_status.json', 'w') as f:
        json.dump(status, f)
```

### 輪詢遠端解鎖

```python
import json
import os

def check_remote_unlock():
    """檢查遠端解鎖（每秒呼叫）"""
    unlock_file = '/tmp/guardian_unlock_status.json'
    
    if not os.path.exists(unlock_file):
        return None
    
    with open(unlock_file, 'r') as f:
        data = json.load(f)
    
    if data.get('remote_unlocked') and data.get('password_correct'):
        return {'success': True, 'method': data.get('unlock_method')}
    
    return None
```

---

## 📱 手機訪問方式

### 同一 WiFi 訪問

**步驟：**
1. TX2 和手機連同一 WiFi
2. TX2 查詢 IP：`hostname -I`
3. 手機瀏覽器：`http://192.168.1.100:8080`

**防火牆設定：**
```bash
sudo ufw allow 5000
```

---

## 🚀 啟動方式

### 統一啟動腳本

```bash
#!/bin/bash
# start_all.sh

# 啟動網頁伺服器（背景）
cd guardian_eye_remote
node server.js &
WEB_PID=$!

echo "✅ 網頁伺服器已啟動"
echo "📱 訪問：http://$(hostname -I | awk '{print $1}'):5000"

# 啟動 Qt
cd ..
python3 main_gui.py

# 關閉服務
kill $WEB_PID
```

---

## 🧪 測試方式

### 模擬警報

```bash
echo '{
  "alarm_active": true,
  "alarm_type": "pig",
  "timestamp": "2025-01-09 14:25:05",
  "confidence": 0.96
}' > /tmp/guardian_alarm_status.json
```

### 測試解鎖

```bash
curl -X POST http://192.168.1.100:5000/api/unlock \
  -H "Content-Type: application/json" \
  -d '{"password": "1234"}'
```

---

## ❓ 常見問題

### Q1：手機無法連線？

**檢查：**
```bash
# 確認服務執行
ps aux | grep node

# 確認 Port
netstat -tuln | grep 5000

# 測試本機
curl http://localhost:5000/health
```

### Q2：更改密碼？

```bash
# 環境變數
export UNLOCK_PASSWORD="mypassword"
node server.js
```

---

## 📊 評分對照

| 講義要求 | 實作 | 符合 |
|---------|------|-----|
| 雙人協同 | 遠端+現場 | ✅ |
| 網頁按鈕 | 完整介面 | ✅ |
| 輪詢機制 | 每秒檢查 | ✅ |

**預期得分：10%（滿分）**

---

## 🎯 開發優先順序

1. **Node.js 伺服器**（1小時）- 必須
2. **Qt 整合**（1小時）- 必須
3. **測試**（1小時）- 必須
4. **Discord Bot**（1-2小時）- 選用

**總時間：3-5 小時**

祝順利！🚀