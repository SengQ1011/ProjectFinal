#include "mcp3008interface.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

Mcp3008Interface::Mcp3008Interface(QObject *parent) : QObject(parent) {
  initSpi();
}

Mcp3008Interface::~Mcp3008Interface() {
  if (m_spi_fd >= 0)
    close(m_spi_fd);

  if (m_fd_sck >= 0) close(m_fd_sck);
  if (m_fd_mosi >= 0) close(m_fd_mosi);
  if (m_fd_miso >= 0) close(m_fd_miso);
  if (m_fd_cs >= 0) close(m_fd_cs);

  if (m_use_bitbang) {
    gpioUnexport(PIN_SCK);
    gpioUnexport(PIN_MISO);
    gpioUnexport(PIN_MOSI);
    gpioUnexport(PIN_CS);
  }
}

void Mcp3008Interface::initSpi() {
  // 1. 優先嘗試 Kernel SPI 驅動
  const char *paths[] = {"/dev/spidev0.0", "/dev/spidev1.0", "/dev/spidev0.1",
                         "/dev/spidev1.1"};

  for (const char *path : paths) {
    m_spi_fd = open(path, O_RDWR);
    if (m_spi_fd >= 0) {
      qDebug() << "Mcp3008Interface: 成功開啟 Kernel SPI 裝置:" << path;
      return;
    }
  }

  // 2. 切換到 Bit-banging 模式
  m_use_bitbang = true;
  qDebug() << "Mcp3008Interface: 使用 Bit-banging 模式 (CLK=427, MOSI=429, "
              "MISO=428, CS=430)";

  int pins[] = {PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS};
  for (int pin : pins) {
    gpioExport(pin);
    QThread::msleep(50); // 給核心一點時間建立檔案節點
  }

  if (gpioSetDirection(PIN_SCK, true) && gpioSetDirection(PIN_MOSI, true) &&
      gpioSetDirection(PIN_CS, true) && gpioSetDirection(PIN_MISO, false)) {

    // 開啟檔案描述符以便快速寫入
    m_fd_sck = open(QString("/sys/class/gpio/gpio%1/value").arg(PIN_SCK).toUtf8().constData(), O_WRONLY);
    m_fd_mosi = open(QString("/sys/class/gpio/gpio%1/value").arg(PIN_MOSI).toUtf8().constData(), O_WRONLY);
    m_fd_miso = open(QString("/sys/class/gpio/gpio%1/value").arg(PIN_MISO).toUtf8().constData(), O_RDONLY);
    m_fd_cs = open(QString("/sys/class/gpio/gpio%1/value").arg(PIN_CS).toUtf8().constData(), O_WRONLY);

    gpioSetValue(PIN_CS, 1);
    gpioSetValue(PIN_SCK, 0);
    qDebug() << "Mcp3008Interface: GPIO 狀態設定完成並已開啟快速存取描述符";
  } else {
    qDebug() << "Mcp3008Interface: GPIO 設定失敗";
  }
}

int Mcp3008Interface::readAdc(int channel) {
  if (m_use_bitbang) {
    return readAdcBitBang(channel);
  }

  if (m_spi_fd < 0 || channel < 0 || channel > 7)
    return -1;

  uint8_t tx[] = {0x01, static_cast<uint8_t>((8 + channel) << 4), 0x00};
  uint8_t rx[3] = {0, 0, 0};

  struct spi_ioc_transfer tr = {
      .tx_buf = (unsigned long)tx,
      .rx_buf = (unsigned long)rx,
      .len = 3,
      .speed_hz = 1000000,
      .delay_usecs = 0,
      .bits_per_word = 8,
  };

  if (ioctl(m_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
    return -1;
  }

  return ((rx[1] & 3) << 8) + rx[2];
}

// 強化版 Bit-banging，加入微秒延遲確保穩定
int Mcp3008Interface::readAdcBitBang(int adcnum) {
  if (adcnum > 7 || adcnum < 0)
    return -1;

  auto delay = []() { usleep(2); }; 

  auto fastSet = [](int fd, int val) {
    if (fd >= 0) {
      const char *v = val ? "1" : "0";
      pwrite(fd, v, 1, 0);
    }
  };

  auto fastGet = [](int fd) {
    if (fd < 0) return 0;
    char val;
    pread(fd, &val, 1, 0);
    return (val == '1') ? 1 : 0;
  };

  fastSet(m_fd_cs, 1);
  fastSet(m_fd_sck, 0);
  delay();
  fastSet(m_fd_cs, 0); // 開始傳輸
  delay();

  int commandout = adcnum;
  commandout |= 0x18; // Start bit + Single-ended bit
  commandout <<= 3;

  // 送出 5 bits 指令
  for (int i = 0; i < 5; i++) {
    fastSet(m_fd_mosi, (commandout & 0x80) ? 1 : 0);
    commandout <<= 1;
    delay();
    fastSet(m_fd_sck, 1);
    delay();
    fastSet(m_fd_sck, 0);
  }

  // 讀取 12 bits (包含 1 個 null bit)
  int adcout = 0;
  for (int i = 0; i < 12; i++) {
    fastSet(m_fd_sck, 1);
    delay();
    fastSet(m_fd_sck, 0);
    delay();
    adcout <<= 1;
    if (fastGet(m_fd_miso)) {
      adcout |= 0x1;
    }
  }

  fastSet(m_fd_cs, 1); // 結束傳輸
  adcout >>= 1; 
  return adcout;
}

// 強化版 Export，避免重複開啟報錯
bool Mcp3008Interface::gpioExport(int pin) {
  QString path = QString("/sys/class/gpio/gpio%1").arg(pin);
  if (QFile::exists(path))
    return true; // 已經存在則跳過

  QFile file("/sys/class/gpio/export");
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QTextStream ts(&file);
  ts << pin;
  file.close();
  return true;
}

bool Mcp3008Interface::gpioUnexport(int pin) {
  QFile file("/sys/class/gpio/unexport");
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QTextStream ts(&file);
  ts << pin;
  file.close();
  return true;
}

bool Mcp3008Interface::gpioSetDirection(int pin, bool out) {
  QFile file(QString("/sys/class/gpio/gpio%1/direction").arg(pin));
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QTextStream ts(&file);
  ts << (out ? "out" : "in");
  file.close();
  return true;
}

bool Mcp3008Interface::gpioSetValue(int pin, int value) {
  QFile file(QString("/sys/class/gpio/gpio%1/value").arg(pin));
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QTextStream ts(&file);
  ts << value;
  file.close();
  return true;
}

int Mcp3008Interface::gpioGetValue(int pin) {
  QFile file(QString("/sys/class/gpio/gpio%1/value").arg(pin));
  if (!file.open(QIODevice::ReadOnly))
    return 0;
  char val;
  file.read(&val, 1);
  file.close();
  return (val == '1') ? 1 : 0;
}
