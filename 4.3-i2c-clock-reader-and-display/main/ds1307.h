/* ds1307.h */

#pragma once

#include <stdint.h>
#include "datetime.h"

#define DS1307_ADDR 0x68

#define DS1307_REG_SECONDS  0x00    /* first of the 7 time registers */

typedef enum { DS1307_OK, DS1307_HALTED, DS1307_BAD_DATA, DS1307_BUS_ERR } ds1307_status_t;

ds1307_status_t ds1307_init(void);
ds1307_status_t ds1307_read(datetime_t *out);
ds1307_status_t ds1307_write(const datetime_t *in);
bool            ds1307_decode(const uint8_t raw[7], datetime_t *out);  /* pure: PC-testable */
void            ds1307_encode(const datetime_t *in, uint8_t raw[7]);