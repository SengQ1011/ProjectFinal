# Guardian Eye - 遠端控制系統

## 專案簡介

Guardian Eye 是一個基於 Node.js 和 Qt 的智慧安全系統，支援遠端警報監控和雙重驗證解鎖機制。

## 功能特色

- **即時警報監控**：透過網頁即時查看系統警報狀態
- **遠端解鎖**：手機輸入密碼 + 現場輸入隨機碼（雙重驗證）
- **快速控制**：靜音警報、強制開門、重置系統等功能
- **歷史記錄**：查看系統事件歷史
- **自動輪詢**：每秒自動更新系統狀態

## 專案結構

```
guardian_eye_remote/
├── server.js              # Node.js 主伺服器
├── discord_bot.js         # Discord Bot（選用）
├── .env.example           # 環境變數範本
├── public/                # 靜態網頁檔案
│   ├── index.html         # 主頁面
│   ├── style.css          # 樣式表
│   └── app.js             # 前端 JavaScript
└── README.md              # 使用說明
```

## 安裝步驟

### 1. 確認 Node.js 已安裝

```bash
node --version  # 應顯示 v16.x.x 或更高
npm --version   # 應顯示 8.x.x 或更高
```

### 2. 安裝依賴（在 TX2 上）

```bash
cd guardian_eye_remote

# 安裝網頁伺服器依賴
npm install express

# 安裝 Discord Bot 依賴（選用，如果要使用 Discord 功能）
npm install discord.js dotenv
```

### 3. 配置環境變數（選用 - Discord Bot）

如果要使用 Discord Bot 功能：

```bash
# 複製環境變數範本
cp .env.example .env

# 編輯 .env 填入你的 Discord Token
nano .env
```

`.env` 內容範例：
```env
DISCORD_TOKEN=你的_Discord_Bot_Token
DISCORD_CHANNEL_ID=你的頻道ID（可選）
UNLOCK_PASSWORD=1234
PORT=8080
```

**取得 Discord Token 步驟：**
1. 前往 [Discord Developer Portal](https://discord.com/developers/applications)
2. 點擊「New Application」建立新應用
3. 進入「Bot」頁面 → 「Add Bot」
4. 在「TOKEN」區域點擊「Reset Token」→ 複製 Token
5. 貼到 `.env` 的 `DISCORD_TOKEN=` 後面
6. 開啟「Privileged Gateway Intents」中的：
   - MESSAGE CONTENT INTENT（必須）
   - SERVER MEMBERS INTENT（建議）

**邀請 Bot 到伺服器：**
1. 在 Developer Portal 進入「OAuth2」→「URL Generator」
2. 勾選 Scopes: `bot`
3. 勾選 Bot Permissions: `Send Messages`, `Read Messages/View Channels`, `Attach Files`, `Embed Links`
4. 複製生成的 URL 到瀏覽器，選擇伺服器邀請 Bot

### 4. 啟動伺服器

```bash
# 只啟動網頁伺服器
node server.js

# 或同時啟動 Discord Bot（另開終端）
node discord_bot.js
```

成功啟動後會顯示：

```
========================================
🛡️  Guardian Eye Remote Control Server
========================================
✅ Server running on http://0.0.0.0:8080
📱 手機訪問（請確保在同一 WiFi）：
   http://192.168.1.100:8080
========================================
📂 靜態檔案目錄: /path/to/public
🔐 解鎖密碼: 1234
========================================
```

## 使用方式

### 方式 1: 網頁控制（必須）

#### 手機訪問

1. 確保手機和 TX2 連接同一 WiFi
2. 在 TX2 上執行 `hostname -I` 取得 IP 位址
3. 在手機瀏覽器輸入：`http://<TX2_IP>:8080`

#### 遠端解鎖流程

1. **警報觸發**：系統偵測到入侵，手機顯示警報
2. **輸入密碼**：在手機網頁輸入解鎖密碼（預設：1234）
3. **驗證通過**：伺服器驗證密碼正確
4. **現場確認**：Qt 介面顯示隨機碼，需在現場輸入
5. **完全解鎖**：兩步驗證完成，系統解除警報

### 方式 2: Discord 控制（選用加分項）

#### Discord Bot 指令

- `!unlock <密碼>` - 遠端解鎖系統
  - 例如：`!unlock 1234`
- `!status` - 查詢系統當前狀態
- `!help` - 顯示幫助訊息

#### Discord 警報推播

當系統偵測到入侵時，Discord Bot 會自動：
1. 發送警報訊息到指定頻道
2. 附加入侵者照片（如果有）
3. 提示使用者輸入解鎖密碼

#### Discord 解鎖流程

1. **收到警報**：Bot 自動推送警報訊息到 Discord
2. **輸入密碼**：在 Discord 輸入 `!unlock 1234`
3. **驗證通過**：Bot 回覆驗證成功
4. **現場確認**：Qt 介面顯示隨機碼
5. **完全解鎖**：在現場輸入隨機碼完成解鎖

## API 文件

### GET /api/status

取得系統當前狀態（每秒輪詢）

**回應範例：**
```json
{
  "alarm_active": true,
  "alarm_type": "pig",
  "alarm_time": "2025-01-09 14:25:05",
  "alarm_confidence": 0.96,
  "remote_unlocked": false,
  "server_time": "2025-01-09T14:25:10.000Z",
  "uptime": 3600
}
```

### POST /api/unlock

遠端解鎖驗證

**請求體：**
```json
{
  "password": "1234"
}
```

**回應範例：**
```json
{
  "success": true,
  "message": "✅ 遠端驗證通過！請回家輸入現場隨機密碼。"
}
```

### GET /api/history

取得歷史記錄

**回應範例：**
```json
[
  {
    "time": "2025-01-09 14:25:05",
    "event": "小豬入侵警報",
    "status": "已解除",
    "type": "alarm"
  }
]
```

### POST /api/control

執行控制指令

**請求體：**
```json
{
  "action": "mute_alarm"
}
```

可用指令：
- `mute_alarm`：靜音警報
- `open_door`：強制開門
- `reset`：重置系統
- `test_alarm`：測試警報

## Qt 整合

### 共享檔案

系統使用以下檔案與 Qt 通訊：

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

#### 3. 控制指令（Node.js 寫入，Qt 讀取）
**檔案：** `/tmp/guardian_control.txt`

內容：單行文字（指令名稱），如：`open_door`

Qt 需每秒檢查此檔案，讀取後執行指令並刪除檔案。

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

Discord Bot 會每秒輪詢此檔案，讀取後發送到 Discord 並刪除檔案。

## 配置選項

### 更改密碼

**方法 1: 使用 .env 檔案（推薦）**
```bash
# 編輯 .env
nano .env

# 修改密碼
UNLOCK_PASSWORD=mypassword
```

**方法 2: 使用環境變數**
```bash
export UNLOCK_PASSWORD="mypassword"
node server.js
```

### 更改端口

**方法 1: 使用 .env 檔案**
```env
PORT=8080
```

**方法 2: 修改程式碼**

修改 [server.js:77](server.js#L77)：
```javascript
const PORT = 8080;  // 改成你想要的端口
```

### 防火牆設定

如果無法連線，需開啟防火牆：

```bash
sudo ufw allow 8080
```

## 測試方式

### 模擬警報

```bash
echo '{
  "alarm_active": true,
  "alarm_type": "pig",
  "timestamp": "2025-01-09 14:25:05",
  "confidence": 0.96
}' > /tmp/guardian_alarm_status.json
```

### 測試解鎖 API

```bash
curl -X POST http://localhost:8080/api/unlock \
  -H "Content-Type: application/json" \
  -d '{"password": "1234"}'
```

### 測試控制指令

```bash
curl -X POST http://localhost:8080/api/control \
  -H "Content-Type: application/json" \
  -d '{"action": "test_alarm"}'
```

## 常見問題

### Q1: 手機無法連線？

**檢查步驟：**

1. 確認服務執行：
   ```bash
   ps aux | grep node
   ```

2. 確認端口監聽：
   ```bash
   netstat -tuln | grep 8080
   ```

3. 測試本機連線：
   ```bash
   curl http://localhost:8080/health
   ```

4. 檢查防火牆：
   ```bash
   sudo ufw status
   ```

### Q2: Qt 讀不到解鎖狀態？

確認檔案權限：
```bash
ls -l /tmp/guardian_unlock_status.json
```

手動測試寫入：
```bash
echo '{"remote_unlocked": true, "password_correct": true}' > /tmp/guardian_unlock_status.json
```

### Q3: 如何重置系統？

```bash
# 刪除所有共享檔案
rm /tmp/guardian_*.json
rm /tmp/guardian_control.txt

# 重啟伺服器
pkill node
node server.js
```

### Q4: Discord Bot 無法啟動？

**檢查步驟：**

1. 確認已安裝依賴：
   ```bash
   npm list discord.js dotenv
   ```

2. 確認 `.env` 存在且已設定 Token：
   ```bash
   cat .env | grep DISCORD_TOKEN
   ```

3. 檢查 Token 是否有效（在 Developer Portal 確認）

4. 確認 Bot 權限：
   - MESSAGE CONTENT INTENT 必須開啟
   - Bot 已加入伺服器

### Q5: Discord Bot 收不到警報？

確認 Qt 有正確寫入佇列檔案：
```bash
# 手動測試
echo '{
  "type": "pig_intrusion",
  "message": "🚨 測試警報",
  "timestamp": "2025-01-09 14:25:05",
  "priority": "high"
}' > /tmp/guardian_discord_queue.json

# 等待 1 秒後檢查檔案是否被刪除（表示 Bot 已讀取）
ls -l /tmp/guardian_discord_queue.json
```

### Q6: TX2 上 npm 安裝很慢？

可以使用淘寶鏡像加速：
```bash
npm config set registry https://registry.npmmirror.com
npm install express discord.js dotenv
```

## 完整啟動腳本

創建 `start_all.sh` 方便一次啟動所有服務：

```bash
#!/bin/bash
# start_all.sh

echo "🚀 啟動 Guardian Eye 系統..."

# 啟動網頁伺服器（背景）
node server.js &
SERVER_PID=$!
echo "✅ 網頁伺服器已啟動 (PID: $SERVER_PID)"

# 啟動 Discord Bot（如果 .env 存在）
if [ -f ".env" ]; then
  node discord_bot.js &
  BOT_PID=$!
  echo "✅ Discord Bot 已啟動 (PID: $BOT_PID)"
fi

echo "========================================"
echo "📱 訪問網址：http://$(hostname -I | awk '{print $1}'):8080"
echo "========================================"
echo "按 Ctrl+C 關閉所有服務"

# 等待中斷信號
trap "echo '\\n👋 正在關閉服務...'; kill $SERVER_PID $BOT_PID 2>/dev/null; exit" SIGINT SIGTERM

wait
```

使用方式：
```bash
chmod +x start_all.sh
./start_all.sh
```

---

**祝使用順利！** 🚀

如有問題請聯繫開發團隊。
