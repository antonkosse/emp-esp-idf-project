#pragma once
#include "driver/i2c_master.h"
/* J1 ESP32-S3-N16R8: той самий ряд, що 3V3 і GND.
Між GPIO8 і GPIO9 на колодці — 3 і 46, їх не брати. */
#define I2C_PORT I2C_NUM_0
#define I2C_SDA_GPIO GPIO_NUM_8
#define I2C_SCL_GPIO GPIO_NUM_9
#define I2C_HZ 100000 /* DS1307: max 100 kHz, не Fast-mode */
#define I2C_TIMEOUT_MS 200
/* Семибітні адреси як у datasheet. ESP-IDF зсуву << 1 не робить (на відміну від HAL F4). */
#define ADDR_SCAN_FROM 0x08 /* NXP: 0x00–0x07 і 0x78–0x7F зарезервовані */
#define ADDR_SCAN_TO 0x77
#define ADDR_OLED 0x3C     /* SSD1306, SA0 = 0 */
#define ADDR_OLED_ALT 0x3D /* той самий контролер, SA0 = 1 */
#define ADDR_RTC 0x68      /* DS1307 */
#define OLED_HEIGHT 64
#define OLED_DEBUG  0     /* 0 = normal program */