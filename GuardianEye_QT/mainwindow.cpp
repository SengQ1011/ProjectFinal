#include "mainwindow.h"
#include "blackboxinterface.h"
#include "emergencycontroller.h"
#include "environmentalcontroller.h"
#include "hardwareinterface.h"
#include "mcp3008interface.h"
#include "pythonaimanager.h"
#include "securitycontroller.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMap>
#include <QMessageBox>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

  // 1. 初始化底層硬體介面 (直接由主執行緒或邏輯執行緒管理)
  blackbox = new BlackboxInterface(this);
  adc = new Mcp3008Interface(this);

  // 2. 初始化業務邏輯控制器
  camera = new PythonAiManager();
  security = new SecurityController();
  env = new EnvironmentalController();
  emergency = new EmergencyController(blackbox);

  // 初始化列表模型 (用於顯示黑盒子事件)
  eventModel = new QStringListModel(this);
  ui->eventTable->setModel(eventModel);

  // 3. 執行緒管理
  cameraThread = new QThread(this);
  logicThread = new QThread(this);

  security->moveToThread(logicThread);
  env->moveToThread(logicThread);

  // 4. 連線設定 (訊號傳遞)

  // Camera -> UI (從 Python 獲取影像與 AI 結果)
  connect(camera, &PythonAiManager::frameReady, this, &MainWindow::updateFrame);
  connect(camera, &PythonAiManager::detectionAlert, this,
          [this](QString type, double conf) {
            // 檢查冷卻時間，避免洗板
            QDateTime now = QDateTime::currentDateTime();
            int cooldown = 60; // 預設 60 秒冷卻

            if (type == "owner")
              cooldown = 10; // 主人 10 秒冷卻

            if (m_lastAlertTime.contains(type) &&
                m_lastAlertTime[type].secsTo(now) < cooldown) {
              return; // 還在冷卻中，不觸發
            }

            if (type == "pig") {
              // 豬豬特別處理：如果炸彈已經啟動，就不再觸發
              if (emergency->isBombActive())
                return;
              m_lastAlertTime[type] = now;
              simulateAiTrigger("pig");
            } else if (type == "stranger") {
              m_lastAlertTime[type] = now;
              simulateAiTrigger("stranger");
            } else if (type == "owner") {
              m_lastAlertTime[type] = now;
              // 主人驗證成功邏輯
              ui->status_label->setText(
                  QString::fromUtf8("狀態: 歡迎主人回家！"));
              handleShortcut(1); // 執行開門動作 (亮綠燈)
            }
          });

  // Security Logic -> Hardware/Log
  connect(security, &SecurityController::requestLog, blackbox,
          &BlackboxInterface::logEvent);
  connect(security, &SecurityController::requestGpio, blackbox,
          &BlackboxInterface::setGpio);
  connect(security, &SecurityController::passwordVerified, this,
          [this](bool success) {
            if (success) {
              ui->status_label->setText(QString::fromUtf8("狀態: 驗證成功！"));

              // 檢查目前是否為豬豬警報 (炸彈啟動中)
              bool wasPigAlarm = emergency->isBombActive();

              // 1. 通用解除動作
              blackbox->setGpio(LED_RED, 0);
              blackbox->setGpio(LED_BLUE, 0);
              blackbox->setGpio(BUZZER, 0);

              // 2. 針對不同警報類型的後續處理
              if (wasPigAlarm) {
                // 豬豬警報：解除炸彈
                emergency->disarmBomb();
              } else {
                // 陌生人警報或其他：執行開門動作
                handleShortcut(1);
              }

              // 清理狀態檔案
              QFile::remove("/tmp/guardian_alarm_status.json");
              QFile::remove("/tmp/guardian_unlock_status.json");

              // 同時解除邏輯鎖定
              QMetaObject::invokeMethod(security, "setAlarmActive",
                                        Q_ARG(bool, false));
            } else {
              ui->status_label->setText(QString::fromUtf8("狀態: 驗證失敗！"));
            }
          });

  // Environmental Logic -> Hardware/UI
  connect(env, &EnvironmentalController::requestGpio, blackbox,
          &BlackboxInterface::setGpio);
  connect(
      env, &EnvironmentalController::lightLevelChanged, this,
      [this](int value, const QString &mode) {
        ui->status_label->setText(
            QString("狀態: 系統運作中 | 亮度: %1 (%2)").arg(value).arg(mode));
      });

  // Emergency Logic -> UI
  connect(emergency, &EmergencyController::countdownUpdated, this,
          [this](int totalSeconds, QString formattedTime) {
            ui->status_label->setText(
                QString("<font color='red'>🚨 緊急倒數: %1 🚨</font>")
                    .arg(formattedTime));

            // 同步更新 JSON 檔案給 Web Server
            updateAlarmJsonWithCountdown(totalSeconds, formattedTime);

            // 倒計時蜂鳴器邏輯：每秒響一下 (200ms)
            if (!m_isMuted) {
              blackbox->setGpio(BUZZER, 1);
              QTimer::singleShot(200, [this]() {
                // 只有在炸彈仍在啟動狀態時才關閉，避免影響其他開門音效
                if (emergency->isBombActive()) {
                  blackbox->setGpio(BUZZER, 0);
                }
              });
            }
          });

  connect(emergency, &EmergencyController::bombExploded, this, [this]() {
    ui->status_label->setText(
        "<font color='red'><b>💥 系統已炸毀 💥</b></font>");
    QMessageBox::critical(this, "警告", "倒數結束，系統已執行緊急自毀程序！");
  });

  connect(emergency, &EmergencyController::bombDisarmed, this,
          [this]() { ui->status_label->setText("狀態: 緊急狀態已解除"); });

  // 5. 啟動執行緒與感測器輪詢
  logicThread->start();

  // 啟動 Python AI 引擎
  camera->start();

  // 感測器輪詢定時器 (在主執行緒中觸發，透過訊號交給邏輯執行緒處理)
  QTimer *sensorTimer = new QTimer(this);
  connect(sensorTimer, &QTimer::timeout, this, &MainWindow::pollSensors);
  sensorTimer->start(1000);

  // 初始化手動 LED 狀態
  m_isAutoLight = true;
  m_manualYellowLed = false;

  setupShortcuts();
  connect(ui->password_input, &QLineEdit::returnPressed, this,
          &MainWindow::handlePasswordInput);
}

MainWindow::~MainWindow() {
  camera->stop();
  // cameraThread 不再用於 PythonAiManager，但若有其他用途可保留
  if (cameraThread->isRunning()) {
    cameraThread->quit();
    cameraThread->wait();
  }

  logicThread->quit();
  logicThread->wait();

  delete ui;
}

void MainWindow::pollSensors() {
  int lightValue = adc->readAdc(0);
  if (lightValue >= 0) {
    QMetaObject::invokeMethod(env, "updateLightLevel", Q_ARG(int, lightValue));
  }

  // --- 新增：讀取黑盒子日誌並更新 UI 列表 ---
  QString newLogs = blackbox->readLogs();
  if (!newLogs.isEmpty()) {
    QStringList newLines = newLogs.split('\n', QString::SkipEmptyParts);
    m_logHistory.append(newLines);

    // 限制顯示筆數，避免記憶體佔用過大 (例如保留最後 100 筆)
    while (m_logHistory.size() > 100) {
      m_logHistory.removeFirst();
    }

    eventModel->setStringList(m_logHistory);
    ui->eventTable->scrollToBottom();
  }

  // --- 新增：檢查 Web Server 的遠端指令 ---
  QFile controlFile("/tmp/guardian_control.txt");
  if (controlFile.exists() &&
      controlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QString action = controlFile.readAll().trimmed();
    controlFile.close();

    qDebug() << "偵測到遠端指令:" << action;

    if (action == "open_door") {
      handleShortcut(1); // 執行亮綠燈邏輯
    } else if (action == "mute_alarm") {
      handleShortcut(2); // 執行靜音邏輯 (僅關閉蜂鳴器與 LED)
      // emergency->disarmBomb(); // 移除：遠端靜音也不應解除炸彈倒數
    } else if (action == "reset") {
      // 重置警報狀態檔案與緊急狀態
      QFile::remove("/tmp/guardian_alarm_status.json");
      emergency->disarmBomb(); // 確保停止 Kernel Driver 的緊急計時與爆炸觸發
      ui->status_label->setText(QString::fromUtf8("狀態: 系統已遠端重置"));
      blackbox->logEvent("系統經由遠端網頁重置", 0);
    } else if (action == "test_alarm") {
      // 模擬 AI 觸發警報
      simulateAiTrigger("pig");
    }

    QFile::remove("/tmp/guardian_control.txt"); // 執行後刪除指令檔案
  }

  // --- 新傳：檢查 遠端解鎖狀態 (僅在警報啟動時有效) ---
  if (QFile::exists("/tmp/guardian_alarm_status.json")) {
    QFile unlockFile("/tmp/guardian_unlock_status.json");
    if (unlockFile.exists() &&
        unlockFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QString data = unlockFile.readAll();
      unlockFile.close();

      // 如果遠端已授權 (remote_unlocked) 且尚未生成現場隨機碼
      QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
      QJsonObject obj = doc.object();

      if (obj["remote_unlocked"].toBool() &&
          !obj["random_code_generated"].toBool()) {

        // 此時才生成隨機碼，並發送至 Discord
        QString code =
            security->generateRandomCode(false); // 不在本地顯示驗證碼
        ui->status_label->setText(
            QString::fromUtf8("遠端授權通過！驗證碼已發送至您的 Discord"));
        sendDiscordCode(code);

        // 更新狀態，標記已生成，避免重複
        obj["random_code_generated"] = true;
        obj["random_code"] = code;

        if (unlockFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
          unlockFile.write(QJsonDocument(obj).toJson());
          unlockFile.close();
        }
      }
    }
  }
}

void MainWindow::setupShortcuts() {
  QShortcut *f1 = new QShortcut(QKeySequence(Qt::Key_F1), this);
  connect(f1, &QShortcut::activated, this, [this]() { handleShortcut(1); });

  QShortcut *f2 = new QShortcut(QKeySequence(Qt::Key_F2), this);
  connect(f2, &QShortcut::activated, this, [this]() { handleShortcut(2); });

  QShortcut *f3 = new QShortcut(QKeySequence(Qt::Key_F3), this);
  connect(f3, &QShortcut::activated, this, [this]() { handleShortcut(3); });

  QShortcut *f4 = new QShortcut(QKeySequence(Qt::Key_F4), this);
  connect(f4, &QShortcut::activated, this, [this]() { handleShortcut(4); });

  // --- 新增：F5 模擬 AI 觸發 ---
  QShortcut *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
  connect(f5, &QShortcut::activated, this, [this]() {
    simulateAiTrigger("pig");
    ui->status_label->setText(QString::fromUtf8("狀態: [F5] 模擬小豬入侵警報"));
  });

  // --- 新增：F6 模擬 陌生人 觸發 ---
  QShortcut *f6 = new QShortcut(QKeySequence(Qt::Key_F6), this);
  connect(f6, &QShortcut::activated, this, [this]() {
    simulateAiTrigger("stranger");
    ui->status_label->setText(QString::fromUtf8("狀態: [F6] 模擬陌生人偵測"));
  });

  // --- 新增：F7 切換自動/手動燈光 ---
  QShortcut *f7 = new QShortcut(QKeySequence(Qt::Key_F7), this);
  connect(f7, &QShortcut::activated, this, [this]() {
    m_isAutoLight = !m_isAutoLight;
    QMetaObject::invokeMethod(env, "setAutoMode", Q_ARG(bool, m_isAutoLight));
    QString modeStr = m_isAutoLight ? "自動 (光敏控制)" : "手動 (快捷鍵控制)";
    ui->status_label->setText(
        QString("狀態: [F7] 燈光模式改為 %1").arg(modeStr));
    blackbox->logEvent(QString("燈光模式切換: %1").arg(modeStr), 0);
  });

  // --- 新增：F8 手動開關黃燈 ---
  QShortcut *f8 = new QShortcut(QKeySequence(Qt::Key_F8), this);
  connect(f8, &QShortcut::activated, this, [this]() {
    if (m_isAutoLight) {
      ui->status_label->setText(
          "狀態: [F8] 目前為自動模式，請先按 F7 切換至手動");
    } else {
      m_manualYellowLed = !m_manualYellowLed;
      QMetaObject::invokeMethod(env, "setManualLed",
                                Q_ARG(bool, m_manualYellowLed));
      QString stateStr = m_manualYellowLed ? "開啟" : "關閉";
      ui->status_label->setText(QString("狀態: [F8] 手動%1黃燈").arg(stateStr));
      blackbox->logEvent(QString("手動%1黃燈").arg(stateStr), 0);
    }
  });
}

void MainWindow::simulateAiTrigger(QString type) {
  // 1. 本地硬體連動 (透過 Blackbox 驅動)
  if (type == "pig") {
    blackbox->setGpio(LED_RED, 1);
    blackbox->logEvent("AI 模擬觸發: 發現小豬入侵 (最高警報)", 2);

    // 初始鳴叫
    if (!m_isMuted) {
      blackbox->setGpio(BUZZER, 1);
      QTimer::singleShot(200, [this]() {
        if (emergency->isBombActive())
          blackbox->setGpio(BUZZER, 0);
      });
    }

    // 啟動 5 分鐘炸彈倒數 (Kernel Timer)
    emergency->triggerPigBomb(5);
  } else if (type == "stranger") {
    blackbox->setGpio(LED_BLUE, 1);
    blackbox->logEvent("AI 模擬觸發: 發現陌生人", 1);
  }

  // 設定 SecurityController 進入警報鎖定狀態
  QMetaObject::invokeMethod(security, "setAlarmActive", Q_ARG(bool, true));

  // 2. 寫入 JSON 給 Web Server 讀取
  QFile file("/tmp/guardian_alarm_status.json");
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&file);
    out.setCodec("UTF-8"); // 確保 UTF-8 編碼
    out << "{\n"
        << "  \"alarm_active\": true,\n"
        << "  \"alarm_type\": \"" << type << "\",\n"
        << "  \"timestamp\": \""
        << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
        << "\",\n"
        << "  \"confidence\": 0.98\n"
        << "}";
    file.close();
    qDebug() << "警報狀態已寫入 /tmp/guardian_alarm_status.json";
  }

  // 3. 同步發送 Discord 推播
  sendDiscordNotification(type, (type == "pig" ? "high" : "normal"));
}

void MainWindow::sendDiscordNotification(QString type, QString priority) {
  QFile queueFile("/tmp/guardian_discord_queue.json");
  if (queueFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QJsonObject jsonObj;
    jsonObj["type"] = (type == "pig" ? "pig_intrusion" : "stranger_detected");
    jsonObj["priority"] = priority;
    jsonObj["message"] =
        (type == "pig") ? QString::fromUtf8("🚨 偵測到小豬入侵！(最高警報)")
                        : QString::fromUtf8("👤 偵測到陌生人來訪");
    jsonObj["timestamp"] =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    jsonObj["image_path"] = "/tmp/guardian_images/alert.jpg";

    QJsonDocument doc(jsonObj);
    queueFile.write(doc.toJson());
    queueFile.close();
    qDebug() << "Discord 推播任務已加入佇列";
  } else {
    qDebug() << "無法寫入 Discord 佇列檔";
  }
}

void MainWindow::sendDiscordCode(QString code) {
  QFile queueFile("/tmp/guardian_discord_queue.json");
  if (queueFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QJsonObject jsonObj;
    jsonObj["type"] = "verification_code";
    jsonObj["priority"] = "high";
    jsonObj["message"] =
        QString::fromUtf8("您的遠端解鎖驗證碼為：**") + code +
        QString::fromUtf8("**\n請在現場設備輸入此代碼以完成解鎖。");
    jsonObj["timestamp"] =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QJsonDocument doc(jsonObj);
    queueFile.write(doc.toJson());
    queueFile.close();
    qDebug() << "驗證碼推播已加入佇列:" << code;
  } else {
    qDebug() << "無法寫入 Discord 佇列檔 (驗證碼)";
  }
}

void MainWindow::updateAlarmJsonWithCountdown(int seconds, QString formatted) {
  QFile file("/tmp/guardian_alarm_status.json");
  // 只有在警報檔案存在時才更新（避免誤建立）
  if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QString content = file.readAll();
    file.close();

    // 簡單替換或解析。這裡採用簡單替換邏輯，確保 Web 端能讀到
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&file);
      out.setCodec("UTF-8");

      // 如果已經有 countdown 欄位則替換，沒有則插入
      if (content.contains("\"countdown\"")) {
        // 正則表達式替換比較穩健，但這裡先用簡單邏輯
        // 實際開發建議使用 QJsonDocument
      }

      // 為了快速演示，我們直接重寫基本的 JSON
      out << "{\n"
          << "  \"alarm_active\": true,\n"
          << "  \"alarm_type\": \"pig\",\n"
          << "  \"countdown\": " << seconds << ",\n"
          << "  \"countdown_str\": \"" << formatted << "\",\n"
          << "  \"timestamp\": \""
          << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
          << "\",\n"
          << "  \"confidence\": 0.98\n"
          << "}";
      file.close();
    }
  }
}

void MainWindow::handleShortcut(int keyId) {
  switch (keyId) {
  case 1:
    ui->status_label->setText("狀態: [F1] 開門中(綠色LED 亮5秒)");
    blackbox->logEvent("開門中(綠色LED 亮5秒)", 0);
    blackbox->setGpio(LED_GREEN, 1);
    QTimer::singleShot(5000, [this]() {
      blackbox->setGpio(LED_GREEN, 0);
      blackbox->logEvent("關門(綠色LED 暗)", 0);
    });
    break;
  case 2:
    m_isMuted = !m_isMuted; // 切換靜音狀態
    if (m_isMuted) {
      ui->status_label->setText("狀態: [F2] 警報已靜音");
      blackbox->logEvent("警報靜音 (F2)", 1);
      blackbox->setGpio(LED_RED, 0);
      blackbox->setGpio(BUZZER, 0);
    } else {
      ui->status_label->setText("狀態: [F2] 警報音效已恢復");
      blackbox->logEvent("恢復警報音效 (F2)", 0);
      if (emergency->isBombActive()) {
        blackbox->setGpio(LED_RED, 1);
      }
    }
    break;
  case 3:
    ui->status_label->setText("狀態: [F3] 查看日誌");
    {
      QString logs = m_logHistory.join("\n");
      QMessageBox::information(this, "黑盒子日誌 (最近 100 筆)",
                               logs.isEmpty() ? "無日誌" : logs);
    }
    break;
  case 4:
    blackbox->logEvent("系統正常關閉", 0);
    qApp->quit();
    break;
  }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_F12) {
    if (m_f12Timer.isValid() && m_f12Timer.elapsed() < 500) {
      // 500ms 內連按兩下 F12
      qDebug() << "F12 連按兩下：手動觸發緊急自毀！";
      ui->status_label->setText(
          "<font color='red'><b>💥 F12 手動觸發自毀程序 💥</b></font>");
      blackbox->logEvent("F12 連按兩下：手動觸發緊急自毀程序", 2);
      blackbox->setGpio(LED_RED, 1);
      emergency->triggerPigBomb(0);           // 立即觸發
      sendDiscordNotification("pig", "high"); // 發送 Discord 通知
      m_f12Timer.invalidate();
    } else {
      m_f12Timer.start();
    }
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::updateFrame(QImage img) {
  if (!img.isNull()) {
    QSize labelSize = ui->video_label->size();
    if (labelSize.width() > 0 && labelSize.height() > 0) {
      ui->video_label->setPixmap(QPixmap::fromImage(img).scaled(
          labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
      ui->video_label->setPixmap(QPixmap::fromImage(img));
    }
  }
}

void MainWindow::handlePasswordInput() {
  QString input = ui->password_input->text();
  QMetaObject::invokeMethod(security, "verifyPassword", Q_ARG(QString, input));
  ui->password_input->clear();
}
