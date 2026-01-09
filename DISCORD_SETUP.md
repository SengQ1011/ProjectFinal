# Discord Bot 完整設定指南

## 第一步：創建 Discord Bot

1. 前往 [Discord Developer Portal](https://discord.com/developers/applications)
2. 點擊「New Application」
3. 輸入應用名稱（例如：Guardian Eye Bot）
4. 點擊「Create」

## 第二步：設定 Bot

1. 在左側選單點擊「Bot」
2. 點擊「Add Bot」→「Yes, do it!」
3. **重要**：在「Privileged Gateway Intents」區域啟用：
   - ✅ **MESSAGE CONTENT INTENT**（必須！否則 Bot 無法讀取訊息內容）
   - ✅ SERVER MEMBERS INTENT（建議開啟）
   - ✅ PRESENCE INTENT（建議開啟）

## 第三步：取得 Token

1. 在「Bot」頁面，找到「TOKEN」區域
2. 點擊「Reset Token」
3. 點擊「Copy」複製 Token
4. **重要**：不要將 Token 分享給任何人！

## 第四步：設定權限並邀請 Bot

### 方法 1: 使用 URL Generator（推薦）

1. 在左側選單點擊「OAuth2」→「URL Generator」
2. 在「SCOPES」區域勾選：
   - ✅ `bot`
3. 在「BOT PERMISSIONS」區域勾選以下權限：

#### General Permissions
- ✅ **View Channels** - 查看頻道

#### Text Permissions
- ✅ **Send Messages** - 發送訊息
- ✅ **Embed Links** - 嵌入連結
- ✅ **Attach Files** - 附加檔案
- ✅ **Read Message History** - 讀取訊息歷史
- ✅ **Add Reactions** - 新增反應（可選）

4. 複製底部生成的 URL
5. 在瀏覽器開啟該 URL
6. 選擇要加入的伺服器
7. 點擊「授權」

### 方法 2: 使用權限整數

如果你已經有權限整數 `412317644864`，可以使用以下 URL：

```
https://discord.com/api/oauth2/authorize?client_id=你的CLIENT_ID&permissions=412317644864&scope=bot
```

**替換步驟：**
1. 前往「OAuth2」→「General」
2. 複製「CLIENT ID」
3. 將上面 URL 中的 `你的CLIENT_ID` 替換為實際的 CLIENT ID
4. 在瀏覽器開啟該 URL 並授權

## 第五步：取得頻道 ID（可選）

如果你想讓 Bot 只監聽特定頻道：

1. 在 Discord 開啟「使用者設定」→「進階」
2. 開啟「開發者模式」
3. 右鍵點擊你想監聽的頻道
4. 點擊「複製頻道 ID」

## 第六步：設定 .env 檔案

在專案目錄創建 `.env` 檔案：

```env
# Discord Bot Token（必須）
DISCORD_TOKEN=你的_Bot_Token

# 監聽的頻道 ID（可選，不設定則監聽所有頻道）
DISCORD_CHANNEL_ID=你的頻道ID

# 解鎖密碼（可選，預設 1234）
UNLOCK_PASSWORD=1234

# 伺服器端口（可選，預設 8080）
PORT=8080
```

## 第七步：測試 Bot

```bash
# 安裝依賴
npm install discord.js dotenv

# 啟動 Bot
node discord_bot.js
```

成功啟動後會顯示：
```
========================================
🤖 Guardian Eye Discord Bot
========================================
✅ Bot 已登入: Guardian Eye Bot#1234
📡 監聽的伺服器數量: 1
📢 監聽所有頻道
🔐 解鎖密碼: 1234
========================================
🔄 開始輪詢 Discord 推播佇列...
```

## 測試指令

在 Discord 頻道輸入：

```
!help
```

Bot 應該會回覆指令列表。

## 常見問題

### Q1: Bot 在線上但不回應？

**原因**：MESSAGE CONTENT INTENT 沒有開啟

**解決方法**：
1. 前往 Developer Portal → Bot
2. 啟用「MESSAGE CONTENT INTENT」
3. 重啟 Bot

### Q2: Bot 無法發送圖片？

**原因**：沒有「Attach Files」權限

**解決方法**：
1. 踢出 Bot
2. 使用新的邀請連結（包含正確權限）重新邀請

### Q3: Bot 看不到訊息？

**原因**：沒有「View Channels」權限，或頻道權限設定問題

**解決方法**：
1. 檢查頻道權限設定
2. 確保 Bot 角色有「查看頻道」權限

### Q4: 如何測試警報推播？

在 TX2 上執行：

```bash
echo '{
  "type": "pig_intrusion",
  "message": "🚨 測試警報",
  "timestamp": "2025-01-09 14:25:05",
  "priority": "high"
}' > /tmp/guardian_discord_queue.json
```

1 秒後 Bot 應該會自動發送訊息到 Discord。

## 權限摘要

最少需要的權限（權限整數：`412317644864`）：

| 權限 | 用途 |
|------|------|
| View Channels | 查看頻道（必須） |
| Send Messages | 發送警報和回覆 |
| Embed Links | 美化訊息格式 |
| Attach Files | 發送入侵者照片 |
| Read Message History | 讀取使用者指令 |
| Add Reactions | 互動確認（可選） |

## 安全提醒

- ❌ 不要將 Token 提交到 Git（已在 .gitignore 排除）
- ❌ 不要在公開場合分享 Token
- ✅ 如果 Token 洩漏，立即在 Developer Portal 重置
- ✅ 定期檢查 Bot 的使用記錄

---

**設定完成！** 🎉

現在你的 Guardian Eye Discord Bot 已經準備就緒。
