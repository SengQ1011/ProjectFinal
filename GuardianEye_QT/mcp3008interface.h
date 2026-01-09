#ifndef MCP3008INTERFACE_H
#define MCP3008INTERFACE_H

#include <QObject>
#include <linux/spi/spidev.h>

class Mcp3008Interface : public QObject {
  Q_OBJECT
public:
  explicit Mcp3008Interface(QObject *parent = nullptr);
  ~Mcp3008Interface();

  int readAdc(int channel);

private:
  int m_spi_fd = -1;
  bool m_use_bitbang = false;

  // SPI Bit-banging 腳位 (根據你的圖片與成功經驗修正)
  const int PIN_SCK = 427;  // Pin 23 (SPI_SCLK)
  const int PIN_MISO = 428; // Pin 21 (SPI_MISO)
  const int PIN_MOSI = 429; // Pin 19 (SPI_MOSI)
  const int PIN_CS = 430;   // Pin 24 (SPI_CE0_N)

  void initSpi();

  // Bit-banging 輔助函數
  int readAdcBitBang(int channel);
  bool gpioExport(int pin);
  bool gpioUnexport(int pin);
  bool gpioSetDirection(int pin, bool out);
  bool gpioSetValue(int pin, int value);
  int gpioGetValue(int pin);
};

#endif // MCP3008INTERFACE_H
