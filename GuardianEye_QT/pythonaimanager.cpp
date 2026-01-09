#include "pythonaimanager.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>

PythonAiManager::PythonAiManager(QObject *parent)
    : QObject(parent), m_process(new QProcess(this)), m_isRunning(false) {

  connect(m_process, &QProcess::readyReadStandardOutput, this,
          &PythonAiManager::handleReadyRead);

  connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
    QByteArray err = m_process->readAllStandardError();
    qDebug() << "Python Error Log:" << err;
  });

  connect(m_process, &QProcess::errorOccurred, this,
          &PythonAiManager::handleProcessError);
  connect(m_process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &PythonAiManager::handleProcessFinished);
}

PythonAiManager::~PythonAiManager() { stop(); }

void PythonAiManager::start() {
  if (m_isRunning)
    return;

  QString appPath = QCoreApplication::applicationDirPath();
  QDir dir(appPath);
  QString workingDir = "";

  qDebug() << "PythonAiManager: 開始尋找 AI 腳本...";
  qDebug() << "Qt 執行位置:" << appPath;

  // 搜尋清單：包含您目前的 GE1_SR 以及原本的 GuardianEye
  QStringList possibleDirs;
  possibleDirs << "GE1_SR" << "GuardianEye" << "GE1";

  for (int i = 0; i < 3; ++i) {
    // 檢查清單中的資料夾
    for (const QString &dirName : possibleDirs) {
      if (dir.exists(dirName)) {
        QDir targetDir(dir.absoluteFilePath(dirName));
        if (targetDir.exists("vision_system.py")) {
          workingDir = targetDir.absolutePath();
          break;
        }
      }
    }
    if (!workingDir.isEmpty())
      break;

    // 檢查當前目錄是否直接包含腳本
    if (dir.exists("vision_system.py")) {
      workingDir = dir.absolutePath();
      break;
    }
    if (!dir.cdUp())
      break;
  }

  if (workingDir.isEmpty()) {
    qDebug() << "PythonAiManager: [錯誤] 找不到 vision_system.py！";
    qDebug() << "請確保 Python 腳本在 GE1_SR 或 GuardianEye 資料夾中。";
    emit errorOccurred("找不到 AI 腳本");
    return;
  }

  m_process->setWorkingDirectory(workingDir);
  QString program = "python3";
  QStringList arguments;
  arguments << "vision_system.py" << "--qt_mode" << "--camera" << "0";

  qDebug() << "PythonAiManager: 成功找到腳本於" << workingDir;
  m_process->start(program, arguments);
  m_isRunning = true;
}

void PythonAiManager::stop() {
  if (!m_isRunning)
    return;

  qDebug() << "PythonAiManager: 正在關閉 AI 系統...";
  m_process->terminate();
  if (!m_process->waitForFinished(3000)) {
    m_process->kill();
  }
  m_isRunning = false;
}

void PythonAiManager::handleReadyRead() {
  m_buffer.append(m_process->readAllStandardOutput());
  int newlineIndex;
  while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
    QByteArray line = m_buffer.left(newlineIndex).trimmed();
    m_buffer.remove(0, newlineIndex + 1);
    if (!line.isEmpty()) {
      parseLine(line);
    }
  }
}

void PythonAiManager::parseLine(const QByteArray &line) {
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(line, &error);
  if (error.error != QJsonParseError::NoError)
    return;

  QJsonObject obj = doc.object();
  if (obj.contains("img")) {
    QString base64Img = obj["img"].toString();
    QByteArray imgData = QByteArray::fromBase64(base64Img.toUtf8());
    QImage img;
    img.loadFromData(imgData, "JPG");
    if (!img.isNull()) {
      emit frameReady(img);
    }
  }

  bool pigDetected = obj["pig_detected"].toBool();
  bool personDetected = obj["person_detected"].toBool();
  QString faceId = obj["face_id"].toString();

  // --- 優化判斷邏輯：優先相信人臉，減少誤報 ---
  if (personDetected) {
    if (faceId.contains("OWNER")) {
      emit detectionAlert("owner", 0.95);
    } else {
      // 這裡不論是 STRANGER 還是 Human 都當作陌生人
      emit detectionAlert("stranger", 0.92);
    }
  } else if (pigDetected) {
    // 只有在沒看到人臉，且看到豬的情況下才直接警報
    emit detectionAlert("pig", 0.99);
  }
}

void PythonAiManager::handleProcessError(QProcess::ProcessError error) {
  if (error == QProcess::Crashed && !m_isRunning) {
    // 忽略在停止過程中觸發的 Crashed 錯誤 (SIGTERM 常見現象)
    return;
  }
  qDebug() << "PythonAiManager: 進程錯誤" << error;
  emit errorOccurred("AI 系統啟動錯誤");
}

void PythonAiManager::handleProcessFinished(int exitCode,
                                            QProcess::ExitStatus exitStatus) {
  if (exitStatus == QProcess::NormalExit || exitCode == 15 || exitCode == 0) {
    qDebug() << "PythonAiManager: AI 系統已正常關閉 (代碼:" << exitCode << ")";
  } else {
    qDebug() << "PythonAiManager: AI 系統異常結束，退出碼:" << exitCode
             << " 狀態:" << exitStatus;
  }
  m_isRunning = false;
}
