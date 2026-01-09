#ifndef BLACKBOXINTERFACE_H
#define BLACKBOXINTERFACE_H

#include "hardwareinterface.h"
#include <QObject>
#include <QString>

class BlackboxInterface : public QObject {
  Q_OBJECT
public:
  explicit BlackboxInterface(QObject *parent = nullptr);
  ~BlackboxInterface();

public slots:
  void logEvent(const QString &message, int priority);
  void setGpio(int pin, int value);
  QString readLogs();

  // 緊急倒數功能
  void startEmergency(int minutes);
  void stopEmergency();
  int getRemainingSeconds();

private:
  int m_fd = -1;
};

#endif // BLACKBOXINTERFACE_H
