#ifndef HARDWAREINTERFACE_H
#define HARDWAREINTERFACE_H

#include <sys/ioctl.h>

// 與 Driver 定義一致的結構
struct event_data {
  char message[256];
  int priority;
};

struct gpio_command {
  int pin;
  int value;
};

#define LOG_EVENT _IOW('B', 1, struct event_data)
#define CLEAR_LOG _IO('B', 2)
#define SET_GPIO_VALUE _IOW('B', 3, struct gpio_command)

// 緊急狀態 ioctl
#define START_EMERGENCY _IOW('B', 4, int)
#define STOP_EMERGENCY _IO('B', 5)
#define GET_EMERGENCY_STATUS _IOR('B', 6, int)

// GPIO 腳位定義
enum GpioPin {
  LED_GREEN = 398,
  LED_BLUE = 389,
  LED_RED = 388,
  LED_YELLOW = 481,
  BUZZER = 297,           // 改為 254
  EXPLOSION_TRIGGER = 298 // 新增爆炸觸發器
};

#endif // HARDWAREINTERFACE_H
