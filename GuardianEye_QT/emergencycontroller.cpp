#include "emergencycontroller.h"
#include <QDebug>

EmergencyController::EmergencyController(BlackboxInterface *interface,
                                         QObject *parent)
    : QObject(parent), m_interface(interface) {

  // 設置輪詢定時器，每 500ms 檢查一次驅動狀態，保持 UI 同步
  m_pollTimer = new QTimer(this);
  connect(m_pollTimer, &QTimer::timeout, this,
          &EmergencyController::pollDriverStatus);
  m_pollTimer->start(500);
}

void EmergencyController::triggerPigBomb(int minutes) {
  qDebug() << "💣 EmergencyController: Triggering Pig Bomb for" << minutes
           << "minutes";
  m_interface->startEmergency(minutes);
  m_isActive = true;
  m_interface->logEvent("小豬炸彈倒數啟動", 2); // CRITICAL priority
}

void EmergencyController::disarmBomb() {
  if (!m_isActive)
    return;

  qDebug() << "🛡️ EmergencyController: Disarming Bomb";
  m_interface->stopEmergency();
  m_isActive = false;
  m_lastRemainingSeconds = 0;
  m_interface->logEvent("炸彈解除成功", 1); // WARNING priority
  emit bombDisarmed();
}

bool EmergencyController::isBombActive() const { return m_isActive; }

int EmergencyController::getRemainingSeconds() const {
  return m_lastRemainingSeconds;
}

void EmergencyController::pollDriverStatus() {
  int seconds = m_interface->getRemainingSeconds();

  // 狀態變化偵測
  if (seconds > 0) {
    if (!m_isActive) {
      m_isActive = true;
    }

    if (seconds != m_lastRemainingSeconds) {
      m_lastRemainingSeconds = seconds;
      emit countdownUpdated(seconds, formatTime(seconds));
    }
  } else {
    // 如果秒數變為 0 且原本是啟動狀態
    if (m_isActive) {
      m_isActive = false;
      m_lastRemainingSeconds = 0;
      emit bombExploded();
      qDebug() << "💥 EmergencyController: BOMB EXPLODED!";
    }
  }
}

QString EmergencyController::formatTime(int totalSeconds) {
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  return QString("%1:%2")
      .arg(minutes, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'));
}
