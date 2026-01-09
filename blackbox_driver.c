#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/ioctl.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/timer.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "blackbox"
#define BUFFER_SIZE 4096

// 緊急倒數相關
#define DEFAULT_COUNTDOWN_MINUTES 5
static struct timer_list emergency_timer;
static int remaining_seconds = 0;
static int emergency_active = 0;
static DEFINE_SPINLOCK(emergency_lock);
static DEFINE_SPINLOCK(log_lock); // 新增：保護日誌緩衝區的鎖

// GPIO 腳位定義 (根據企劃書)
#define LED_GREEN 398
#define LED_BLUE 389
#define LED_RED 388
#define LED_YELLOW 481
#define BUZZER 297
#define EXPLOSION_TRIGGER 298 // 原蜂鳴器改為爆炸觸發器

// ioctl 定義
struct event_data {
  char message[256];
  int priority; // 0=INFO, 1=WARNING, 2=CRITICAL
};

struct gpio_command {
  int pin;
  int value; // 0 為低電位 (OFF), 1 為高電位 (ON)
};

#define LOG_EVENT _IOW('B', 1, struct event_data)
#define CLEAR_LOG _IO('B', 2)
#define SET_GPIO_VALUE _IOW('B', 3, struct gpio_command)

// 緊急狀態 ioctl
#define START_EMERGENCY _IOW('B', 4, int) // 傳入分鐘數
#define STOP_EMERGENCY _IO('B', 5)
#define GET_EMERGENCY_STATUS _IOR('B', 6, int) // 取得剩餘秒數

static int major;
static char *log_buffer;
static int write_ptr = 0;
static int read_ptr = 0;
static int is_full = 0;

// 輔助函式：寫入緩衝區
static void write_to_buffer(const char *text) {
  unsigned long flags;
  spin_lock_irqsave(&log_lock, flags);

  while (*text) {
    log_buffer[write_ptr] = *text++;
    write_ptr = (write_ptr + 1) % BUFFER_SIZE;
    if (write_ptr == read_ptr) {
      // 如果寫入追上讀取，表示滿了，強制移動讀取指標（覆蓋舊資料）
      read_ptr = (read_ptr + 1) % BUFFER_SIZE;
      is_full = 1;
    } else {
      is_full = 0;
    }
  }

  spin_unlock_irqrestore(&log_lock, flags);
}

// Timer 回調函數：每秒執行一次
static void emergency_timer_callback(unsigned long data) {
  unsigned long flags;
  int trigger_explosion = 0;
  int current_seconds = 0;

  spin_lock_irqsave(&emergency_lock, flags);
  if (emergency_active && remaining_seconds > 0) {
    remaining_seconds--;
    current_seconds = remaining_seconds;
    if (remaining_seconds == 0) {
      trigger_explosion = 1;
    } else {
      mod_timer(&emergency_timer, jiffies + msecs_to_jiffies(1000));
    }
  }
  spin_unlock_irqrestore(&emergency_lock, flags);

  // 在鎖外執行 GPIO 操作，避免 scheduling while atomic
  if (trigger_explosion) {
    printk(KERN_CRIT
           "Blackbox: EMERGENCY COUNTDOWN REACHED ZERO! BOMB ACTIVATED!\n");
    gpio_set_value(EXPLOSION_TRIGGER, 1);
    gpio_set_value(BUZZER, 1);
    gpio_set_value(LED_RED, 1);
  } else if (emergency_active && current_seconds > 0) {
    gpio_set_value(LED_RED, current_seconds % 2);
  }
}

static int dev_open(struct inode *inodep, struct file *filep) { return 0; }

static int dev_release(struct inode *inodep, struct file *filep) { return 0; }

static ssize_t dev_read(struct file *filep, char *buffer, size_t len,
                        loff_t *offset) {
  size_t available;
  int bytes_to_read;
  int i;
  unsigned long flags;

  spin_lock_irqsave(&log_lock, flags);

  if (write_ptr == read_ptr && !is_full) {
    spin_unlock_irqrestore(&log_lock, flags);
    return 0; // 空的
  }

  if (is_full) {
    available = BUFFER_SIZE;
  } else {
    available = (write_ptr - read_ptr + BUFFER_SIZE) % BUFFER_SIZE;
  }

  bytes_to_read = (len < available) ? len : available;

  for (i = 0; i < bytes_to_read; i++) {
    char data = log_buffer[read_ptr];
    read_ptr = (read_ptr + 1) % BUFFER_SIZE;
    is_full = 0;
    spin_unlock_irqrestore(&log_lock, flags);

    if (copy_to_user(&buffer[i], &data, 1))
      return -EFAULT;

    spin_lock_irqsave(&log_lock, flags);
  }

  spin_unlock_irqrestore(&log_lock, flags);
  return bytes_to_read;
}

static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
  struct event_data event;
  struct gpio_command g_cmd;
  struct timespec64 ts;
  struct rtc_time tm;
  char timestamped_msg[512];

  switch (cmd) {
  case LOG_EVENT:
    if (copy_from_user(&event, (struct event_data *)arg,
                       sizeof(struct event_data))) {
      return -EFAULT;
    }

    // 取得當前時間
    ktime_get_real_ts64(&ts);
    rtc_time64_to_tm(ts.tv_sec, &tm);

    // 格式化訊息：[時間] [優先級] 訊息
    snprintf(timestamped_msg, sizeof(timestamped_msg),
             "[%04d-%02d-%02d %02d:%02d:%02d] PRIO:%d MSG:%s\n",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec, event.priority, event.message);

    write_to_buffer(timestamped_msg);
    printk(KERN_INFO "Blackbox: Logged event - %s\n", event.message);
    break;

  case CLEAR_LOG: {
    unsigned long flags;
    spin_lock_irqsave(&log_lock, flags);
    write_ptr = 0;
    read_ptr = 0;
    is_full = 0;
    memset(log_buffer, 0, BUFFER_SIZE);
    spin_unlock_irqrestore(&log_lock, flags);
    printk(KERN_INFO "Blackbox: Log cleared\n");
    break;
  }

  case SET_GPIO_VALUE:
    if (copy_from_user(&g_cmd, (struct gpio_command *)arg,
                       sizeof(struct gpio_command))) {
      return -EFAULT;
    }
    if (gpio_is_valid(g_cmd.pin)) {
      gpio_set_value(g_cmd.pin, g_cmd.value);
    }
    break;

  case START_EMERGENCY: {
    int minutes;
    unsigned long flags;
    int immediate_trigger = 0;
    if (copy_from_user(&minutes, (int *)arg, sizeof(int))) {
      return -EFAULT;
    }

    spin_lock_irqsave(&emergency_lock, flags);
    if (!emergency_active) {
      emergency_active = 1;
      remaining_seconds = minutes * 60;
      if (remaining_seconds == 0) {
        immediate_trigger = 1;
      } else {
        mod_timer(&emergency_timer, jiffies + msecs_to_jiffies(1000));
        printk(KERN_INFO
               "Blackbox: Emergency countdown started for %d minutes\n",
               minutes);
      }
    }
    spin_unlock_irqrestore(&emergency_lock, flags);

    // 移出鎖外執行
    if (immediate_trigger) {
      printk(KERN_CRIT "Blackbox: IMMEDIATE BOMB ACTIVATED!\n");
      gpio_set_value(EXPLOSION_TRIGGER, 1);
      gpio_set_value(BUZZER, 1);
      gpio_set_value(LED_RED, 1);
    }
    break;
  }

  case STOP_EMERGENCY: {
    unsigned long flags;
    spin_lock_irqsave(&emergency_lock, flags);
    emergency_active = 0;
    remaining_seconds = 0;
    del_timer(&emergency_timer);
    spin_unlock_irqrestore(&emergency_lock, flags);

    // 移出鎖外執行
    gpio_set_value(EXPLOSION_TRIGGER, 0); // 停止時關閉爆炸觸發器
    gpio_set_value(BUZZER, 0);            // 停止時關閉蜂鳴器
    gpio_set_value(LED_RED, 0);
    printk(KERN_INFO "Blackbox: Emergency countdown stopped\n");
    break;
  }

  case GET_EMERGENCY_STATUS: {
    if (copy_to_user((int *)arg, &remaining_seconds, sizeof(int))) {
      return -EFAULT;
    }
    break;
  }

  default:
    return -EINVAL;
  }
  return 0;
}

static struct file_operations fops = {
    .open = dev_open,
    .release = dev_release,
    .read = dev_read,
    .unlocked_ioctl = dev_ioctl,
};

static int __init blackbox_init(void) {
  major = register_chrdev(0, DEVICE_NAME, &fops);
  if (major < 0) {
    printk(KERN_ALERT "Blackbox: Failed to register major number\n");
    return major;
  }

  log_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
  if (!log_buffer) {
    unregister_chrdev(major, DEVICE_NAME);
    return -ENOMEM;
  }

  // 初始化 GPIO
  if (gpio_request(LED_GREEN, "LED_GREEN") < 0)
    printk(KERN_ERR "Blackbox: Failed to request LED_GREEN\n");
  gpio_direction_output(LED_GREEN, 0);

  if (gpio_request(LED_BLUE, "LED_BLUE") < 0)
    printk(KERN_ERR "Blackbox: Failed to request LED_BLUE\n");
  gpio_direction_output(LED_BLUE, 0);

  if (gpio_request(LED_RED, "LED_RED") < 0)
    printk(KERN_ERR "Blackbox: Failed to request LED_RED\n");
  gpio_direction_output(LED_RED, 0);

  if (gpio_request(LED_YELLOW, "LED_YELLOW") < 0)
    printk(KERN_ERR "Blackbox: Failed to request LED_YELLOW\n");
  gpio_direction_output(LED_YELLOW, 0);

  if (gpio_request(BUZZER, "BUZZER") < 0)
    printk(KERN_ERR "Blackbox: Failed to request BUZZER\n");
  gpio_direction_output(BUZZER, 0);

  if (gpio_request(EXPLOSION_TRIGGER, "EXPLOSION_TRIGGER") < 0)
    printk(KERN_ERR "Blackbox: Failed to request EXPLOSION_TRIGGER\n");
  gpio_direction_output(EXPLOSION_TRIGGER, 0);

  // 初始化 Timer
  setup_timer(&emergency_timer, emergency_timer_callback, 0);

  memset(log_buffer, 0, BUFFER_SIZE);
  printk(KERN_INFO "Blackbox: Module loaded with major %d and GPIOs ready\n",
         major);
  return 0;
}

static void __exit blackbox_exit(void) {
  // 停止 Timer
  del_timer(&emergency_timer);

  // 釋放 GPIO
  gpio_free(LED_GREEN);
  gpio_free(LED_BLUE);
  gpio_free(LED_RED);
  gpio_free(LED_YELLOW);
  gpio_free(BUZZER);
  gpio_free(EXPLOSION_TRIGGER);

  kfree(log_buffer);
  unregister_chrdev(major, DEVICE_NAME);
  printk(KERN_INFO "Blackbox: Module unloaded\n");
}

module_init(blackbox_init);
module_exit(blackbox_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("GuardianEye Team");
MODULE_DESCRIPTION(
    "A circular buffer log driver with GPIO support for TX2 project");
