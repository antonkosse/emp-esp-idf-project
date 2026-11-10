/* i2c_hal.h */
#pragma once

#include <stdint.h>

typedef enum { I2C_OK, I2C_NACK, I2C_TIMEOUT, I2C_ERR } i2c_status_t;

bool         i2c_hal_init(void);
i2c_status_t i2c_hal_probe(uint8_t addr7);
bool         i2c_hal_add(uint8_t addr7);     /* idempotent: second call reuses the handle */
i2c_status_t i2c_hal_write(uint8_t addr7, const uint8_t *data, size_t len);
i2c_status_t i2c_hal_write_read(uint8_t addr7, const uint8_t *wr, size_t wlen,
                                uint8_t *rd, size_t rlen);