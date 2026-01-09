#ifndef SECURITYCONTROLLER_H
#define SECURITYCONTROLLER_H

#include <QObject>
#include <QString>

class SecurityController : public QObject {
  Q_OBJECT
public:
  explicit SecurityController(QObject *parent = nullptr);

public slots:
  void verifyPassword(const QString &input);
  QString generateRandomCode(bool showInLog = true);
  void setRandomCode(const QString &code) { m_currentRandomCode = code; }
  void setAlarmActive(bool active) { m_isAlarmActive = active; }

signals:
  void passwordVerified(bool success);
  void requestLog(const QString &message, int priority);
  void requestGpio(int pin, int value);

private:
  QString m_masterPassword = "1234";
  QString m_currentRandomCode = "";
  bool m_isAlarmActive = false;
};

#endif // SECURITYCONTROLLER_H
