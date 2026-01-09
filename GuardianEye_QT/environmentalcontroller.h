#ifndef ENVIRONMENTALCONTROLLER_H
#define ENVIRONMENTALCONTROLLER_H

#include <QObject>

class EnvironmentalController : public QObject {
  Q_OBJECT
public:
  explicit EnvironmentalController(QObject *parent = nullptr);

public slots:
  void updateLightLevel(int value);
  void setAutoMode(bool enabled);
  void setManualLed(bool on);

signals:
  void lightLevelChanged(int value, const QString &mode);
  void autoModeChanged(bool enabled);
  void requestGpio(int pin, int value);

private:
  int m_lastValue = -1;
  bool m_autoMode = true;
  bool m_manualLedState = false;
};

#endif // ENVIRONMENTALCONTROLLER_H
