#include "blackboxinterface.h"
#include <QDebug>
#include <QFile>
#include <fcntl.h>
#include <unistd.h>

BlackboxInterface::BlackboxInterface(QObject *parent) : QObject(parent) {
  m_fd = open("/dev/blackbox", O_RDWR);
  if (m_fd < 0) {
    qDebug() << "BlackboxInterface: 無法開啟 /dev/blackbox";
  }
}

BlackboxInterface::~BlackboxInterface() {
  if (m_fd >= 0)
    close(m_fd);
}

void BlackboxInterface::logEvent(const QString &message, int priority) {
  if (m_fd < 0)
    return;

  struct event_data event;
  strncpy(event.message, message.toUtf8().constData(), 255);
  event.message[255] = '\0';
  event.priority = priority;

  if (ioctl(m_fd, LOG_EVENT, &event) < 0) {
    qDebug() << "BlackboxInterface: ioctl 寫入失敗";
  }
}

void BlackboxInterface::setGpio(int pin, int value) {
  if (m_fd < 0)
    return;

  struct gpio_command cmd;
  cmd.pin = pin;
  cmd.value = value;

  if (ioctl(m_fd, SET_GPIO_VALUE, &cmd) < 0) {
    qDebug() << "BlackboxInterface: GPIO ioctl 失敗";
  }
}

QString BlackboxInterface::readLogs() {
  QFile file("/dev/blackbox");
  if (!file.open(QIODevice::ReadOnly)) {
    return "無法讀取日誌";
  }
  return QString::fromUtf8(file.readAll());
}

void BlackboxInterface::startEmergency(int minutes) {
  if (m_fd < 0)
    return;
  if (ioctl(m_fd, START_EMERGENCY, &minutes) < 0) {
    qDebug() << "BlackboxInterface: START_EMERGENCY ioctl 失敗";
  }
}

void BlackboxInterface::stopEmergency() {
  if (m_fd < 0)
    return;
  if (ioctl(m_fd, STOP_EMERGENCY) < 0) {
    qDebug() << "BlackboxInterface: STOP_EMERGENCY ioctl 失敗";
  }
}

int BlackboxInterface::getRemainingSeconds() {
  if (m_fd < 0)
    return 0;
  int seconds = 0;
  if (ioctl(m_fd, GET_EMERGENCY_STATUS, &seconds) < 0) {
    qDebug() << "BlackboxInterface: GET_EMERGENCY_STATUS ioctl 失敗";
    return 0;
  }
  return seconds;
}
