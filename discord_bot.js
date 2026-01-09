// discord_bot.js
const { Client, GatewayIntentBits, AttachmentBuilder, EmbedBuilder } = require("discord.js");
const fs = require("fs");
const path = require("path");

// 載入環境變數
require("dotenv").config();

// ========================= 配置 =========================

// Discord Bot Token（從 .env 讀取）
const DISCORD_TOKEN = process.env.DISCORD_TOKEN;

// 監聽的頻道 ID（可選，不設定則監聽所有頻道）
const CHANNEL_ID = process.env.DISCORD_CHANNEL_ID || null;

// 解鎖密碼
const CORRECT_PASSWORD = process.env.UNLOCK_PASSWORD || "1234";

// 輪詢間隔（毫秒）
const POLL_INTERVAL = 1000; // 1 秒

// ========================= 共享檔案路徑 =========================

// const UNLOCK_STATUS_FILE = "/tmp/guardian_unlock_status.json";
// const DISCORD_QUEUE_FILE = "/tmp/guardian_discord_queue.json";
// const ALARM_STATUS_FILE = "/tmp/guardian_alarm_status.json";
const UNLOCK_STATUS_FILE = path.join(__dirname, "guardian_unlock_status.json");
const DISCORD_QUEUE_FILE = path.join(__dirname, "guardian_discord_queue.json");
const ALARM_STATUS_FILE = path.join(__dirname, "guardian_alarm_status.json");


// ========================= Discord Client =========================

const client = new Client({
  intents: [
    GatewayIntentBits.Guilds,
    GatewayIntentBits.GuildMessages,
    GatewayIntentBits.MessageContent,
  ],
});

// ========================= 工具函數 =========================

/**
 * 寫入解鎖狀態（供 Qt 輪詢）
 */
function writeUnlockStatus(data) {
  try {
    fs.writeFileSync(UNLOCK_STATUS_FILE, JSON.stringify(data, null, 2));
    console.log("解鎖狀態已更新:", data);
    return true;
  } catch (error) {
    console.error("❌ 寫入解鎖狀態失敗:", error.message);
    return false;
  }
}

/**
 * 讀取並處理 Discord 推播佇列
 */
function processDiscordQueue() {
  if (!fs.existsSync(DISCORD_QUEUE_FILE)) {
    return null;
  }

  try {
    const data = fs.readFileSync(DISCORD_QUEUE_FILE, "utf8");
    const queueData = JSON.parse(data);

    // 刪除檔案（表示已讀取）
    fs.unlinkSync(DISCORD_QUEUE_FILE);

    return queueData;
  } catch (error) {
    console.error("❌ 讀取 Discord 佇列失敗:", error.message);
    // 如果解析失敗，也刪除檔案避免重複錯誤
    try {
      if (fs.existsSync(DISCORD_QUEUE_FILE)) {
        fs.unlinkSync(DISCORD_QUEUE_FILE);
      }
    } catch (e) {
      console.error("❌ 刪除錯誤佇列檔案失敗:", e.message);
    }
    return null;
  }
}

/**
 * 發送警報訊息到 Discord
 */
async function sendAlertToDiscord(queueData) {
  if (!client.isReady()) {
    console.error("❌ Discord Bot 尚未就緒");
    return;
  }

  try {
    // 取得目標頻道
    let channel;
    if (CHANNEL_ID) {
      channel = await client.channels.fetch(CHANNEL_ID);
    } else {
      // 如果沒有指定頻道，發送到第一個可用的文字頻道
      const guild = client.guilds.cache.first();
      if (!guild) {
        console.error("❌ 找不到任何伺服器");
        return;
      }
      channel = guild.channels.cache.find(ch => ch.isTextBased());
    }

    if (!channel) {
      console.error("❌ 找不到目標頻道");
      return;
    }

    // 建立嵌入訊息
    const embed = new EmbedBuilder()
      .setTitle(getAlertTitle(queueData.type))
      .setDescription(queueData.message || "系統警報")
      .setColor(queueData.priority === "high" ? 0xFF0000 : 0xFFA500)
      .setTimestamp(new Date(queueData.timestamp || Date.now()))
      .addFields(
        { name: "⏰ 時間", value: queueData.timestamp || "未知", inline: true },
        { name: "🔔 優先級", value: queueData.priority || "normal", inline: true }
      );

    // 如果有圖片，附加圖片
    const messageOptions = { embeds: [embed] };

    if (queueData.image_path && fs.existsSync(queueData.image_path)) {
      const attachment = new AttachmentBuilder(queueData.image_path);
      messageOptions.files = [attachment];
      embed.setImage(`attachment://${path.basename(queueData.image_path)}`);
    }

    // 發送訊息
    await channel.send(messageOptions);
    console.log(`✅ 警報已發送到 Discord: ${queueData.type}`);

    // 發送解鎖提示
    await channel.send(
      "🔐 **請輸入密碼進行遠端解鎖**\n" +
      "格式: `!unlock 您的密碼`\n" +
      "例如: `!unlock 1234`"
    );

  } catch (error) {
    console.error("❌ 發送 Discord 訊息失敗:", error.message);
  }
}

/**
 * 取得警報標題
 */
function getAlertTitle(type) {
  const titles = {
    "pig_intrusion": "🐷 小豬玩偶入侵警報！",
    "stranger_detected": "👤 陌生人偵測警報！",
    "motion_detected": "🚶 動態偵測警報！",
    "door_forced": "🚪 強制開門警報！",
  };
  return titles[type] || "🚨 系統警報";
}

/**
 * 驗證解鎖密碼
 */
function verifyPassword(password) {
  return password === CORRECT_PASSWORD;
}

// ========================= Discord 事件處理 =========================

/**
 * Bot 就緒事件
 */
client.once("ready", () => {
  console.log("========================================");
  console.log("🤖 Guardian Eye Discord Bot");
  console.log("========================================");
  console.log(`✅ Bot 已登入: ${client.user.tag}`);
  console.log(`📡 監聽的伺服器數量: ${client.guilds.cache.size}`);

  if (CHANNEL_ID) {
    console.log(`📢 監聽頻道 ID: ${CHANNEL_ID}`);
  } else {
    console.log("📢 監聽所有頻道");
  }

  console.log(`🔐 解鎖密碼: ${CORRECT_PASSWORD}`);
  console.log("========================================");

  // 啟動佇列輪詢
  startQueuePolling();
});

/**
 * 訊息事件處理
 */
client.on("messageCreate", async (message) => {
  // 忽略 Bot 自己的訊息
  if (message.author.bot) return;

  // 如果設定了特定頻道，只處理該頻道的訊息
  if (CHANNEL_ID && message.channel.id !== CHANNEL_ID) return;

  const content = message.content.trim();

  // 處理解鎖指令: !unlock <密碼>
  if (content.startsWith("!unlock")) {
    const parts = content.split(/\s+/);

    if (parts.length < 2) {
      await message.reply("❌ 請提供密碼！\n格式: `!unlock 您的密碼`");
      return;
    }

    const password = parts[1];

    // 驗證密碼
    if (verifyPassword(password)) {
      // 寫入解鎖狀態
      const unlockData = {
        remote_unlocked: true,
        password_correct: true,
        timestamp: new Date().toISOString(),
        unlock_method: "discord",
        user: message.author.tag,
      };

      const success = writeUnlockStatus(unlockData);

      if (success) {
        const successEmbed = new EmbedBuilder()
          .setTitle("✅ 遠端驗證通過！")
          .setDescription("請返回現場輸入隨機密碼以完成解鎖。")
          .setColor(0x00FF00)
          .setTimestamp()
          .addFields(
            { name: "👤 解鎖者", value: message.author.tag, inline: true },
            { name: "⏰ 時間", value: new Date().toLocaleString("zh-TW"), inline: true }
          );

        await message.reply({ embeds: [successEmbed] });
        console.log(`✅ Discord 解鎖成功: ${message.author.tag}`);
      } else {
        await message.reply("❌ 系統錯誤，無法寫入解鎖狀態。");
      }
    } else {
      // 密碼錯誤
      const errorEmbed = new EmbedBuilder()
        .setTitle("❌ 密碼錯誤")
        .setDescription("請檢查密碼後重試。")
        .setColor(0xFF0000)
        .setTimestamp();

      await message.reply({ embeds: [errorEmbed] });
      console.log(`❌ Discord 解鎖失敗（密碼錯誤）: ${message.author.tag}`);
    }
  }

  // 處理狀態查詢指令: !status
  else if (content === "!status") {
    try {
      let statusText = "📊 **Guardian Eye 系統狀態**\n\n";

      // 讀取警報狀態
      if (fs.existsSync(ALARM_STATUS_FILE)) {
        const alarmData = JSON.parse(fs.readFileSync(ALARM_STATUS_FILE, "utf8"));
        statusText += `🚨 警報狀態: ${alarmData.alarm_active ? "**啟動中**" : "正常"}\n`;
        if (alarmData.alarm_active) {
          statusText += `📋 警報類型: ${alarmData.alarm_type}\n`;
          statusText += `⏰ 觸發時間: ${alarmData.timestamp}\n`;
          statusText += `📊 信心度: ${(alarmData.confidence * 100).toFixed(1)}%\n`;
        }
      } else {
        statusText += "🚨 警報狀態: 正常\n";
      }

      // 讀取解鎖狀態
      if (fs.existsSync(UNLOCK_STATUS_FILE)) {
        const unlockData = JSON.parse(fs.readFileSync(UNLOCK_STATUS_FILE, "utf8"));
        statusText += `🔓 遠端解鎖: ${unlockData.remote_unlocked ? "**已解鎖**" : "未解鎖"}\n`;
        if (unlockData.remote_unlocked) {
          statusText += `⏰ 解鎖時間: ${unlockData.timestamp}\n`;
          statusText += `📱 解鎖方式: ${unlockData.unlock_method}\n`;
        }
      } else {
        statusText += "🔓 遠端解鎖: 未解鎖\n";
      }

      const statusEmbed = new EmbedBuilder()
        .setTitle("📊 系統狀態")
        .setDescription(statusText)
        .setColor(0x00BFFF)
        .setTimestamp();

      await message.reply({ embeds: [statusEmbed] });
    } catch (error) {
      await message.reply("❌ 無法讀取系統狀態。");
      console.error("❌ 讀取狀態失敗:", error.message);
    }
  }

  // 處理幫助指令: !help
  else if (content === "!help") {
    const helpEmbed = new EmbedBuilder()
      .setTitle("🤖 Guardian Eye Discord Bot 指令說明")
      .setDescription("以下是可用的指令：")
      .setColor(0x5865F2)
      .addFields(
        { name: "!unlock <密碼>", value: "遠端解鎖系統\n例如: `!unlock 1234`" },
        { name: "!status", value: "查詢系統當前狀態" },
        { name: "!help", value: "顯示此幫助訊息" }
      )
      .setFooter({ text: "Guardian Eye Security System" })
      .setTimestamp();

    await message.reply({ embeds: [helpEmbed] });
  }
});

/**
 * 錯誤處理
 */
client.on("error", (error) => {
  console.error("❌ Discord Client 錯誤:", error.message);
});

process.on("unhandledRejection", (error) => {
  console.error("❌ 未處理的 Promise 拒絕:", error);
});

// ========================= 佇列輪詢 =========================

/**
 * 啟動佇列輪詢（每秒檢查一次）
 */
function startQueuePolling() {
  console.log("🔄 開始輪詢 Discord 推播佇列...");

  setInterval(() => {
    const queueData = processDiscordQueue();

    if (queueData) {
      console.log("📨 收到新的警報佇列:", queueData.type);
      sendAlertToDiscord(queueData);
    }
  }, POLL_INTERVAL);
}

// ========================= 啟動 Bot =========================

/**
 * 登入 Discord
 */
function startBot() {
  if (!DISCORD_TOKEN) {
    console.error("========================================");
    console.error("❌ 錯誤：未設定 Discord Bot Token");
    console.error("========================================");
    console.error("請在 .env 檔案中設定 DISCORD_TOKEN");
    console.error("步驟：");
    console.error("1. 複製 .env.example 為 .env");
    console.error("2. 前往 https://discord.com/developers/applications");
    console.error("3. 建立新應用程式並在 Bot 頁面取得 Token");
    console.error("4. 在 .env 中填入: DISCORD_TOKEN=你的token");
    console.error("========================================");
    process.exit(1);
  }

  client.login(DISCORD_TOKEN).catch((error) => {
    console.error("❌ Bot 登入失敗:", error.message);
    process.exit(1);
  });
}

// 優雅關閉
process.on("SIGINT", () => {
  console.log("\n\n👋 Discord Bot 正在關閉...");
  client.destroy();
  process.exit(0);
});

// 啟動
startBot();
