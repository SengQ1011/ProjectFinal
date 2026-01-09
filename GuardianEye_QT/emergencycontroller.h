#ifndef EMERGENCYCONTROLLER_H
#define EMERGENCYCONTROLLER_H

#include "blackboxinterface.h"
#include <QObject>
#include <QTimer>


/**
 * EmergencyController (OOP)
 * 負責協調驅動層與 UI 層的緊急倒數狀態
 */
class EmergencyController : public QObject {
  Q_OBJECT
public:
  explicit EmergencyController(BlackboxInterface *interface,
                               QObject *parent = nullptr);

  // 業務邏輯接口
  void triggerPigBomb(int minutes = 5);
  void disarmBomb();
  bool isBombActive() const;
  int getRemainingSeconds() const;

signals:
  void countdownUpdated(int totalSeconds, QString formattedTime);
  void bombExploded();
  void bombDisarmed();

private slots:
  void pollDriverStatus();

private:
  BlackboxInterface *m_interface;
  QTimer *m_pollTimer;
  int m_lastRemainingSeconds = 0;
  bool m_isActive = false;

  QString formatTime(int totalSeconds);
};

#endif // EMERGENCYCONTROLLER_H
