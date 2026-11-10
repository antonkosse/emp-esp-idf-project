#pragma once
#include "driver/i2c_master.h"          // for I2C_NUM_x / GPIO_NUM_x

/* I2C bus */
#define I2C_PORT             I2C_NUM_0
#define I2C_SDA_GPIO         GPIO_NUM_8
#define I2C_SCL_GPIO         GPIO_NUM_9
#define I2C_HZ               100000      // DS1307 max 100 kHz
#define I2C_TIMEOUT_MS       200         // > longest transfer (~92 ms full frame)
#define I2C_INTERNAL_PULLUP  1           // 0 = diagnostic: module pull-ups only

/* OLED (address depends on the module's SA0 resistor) */
#define ADDR_OLED            0x3C
#define ADDR_OLED_ALT        0x3D
#define OLED_HEIGHT          64

/* App */
#define POLL_MS              250         // RTC poll period
#define RTC_SET_FROM_BUILD   1           // 1 = write build time once, then back to 0
/* optional: #define RTC_SQW_GPIO  GPIO_NUM_x */

_Static_assert(POLL_MS <= 500, "poll at least twice per second");