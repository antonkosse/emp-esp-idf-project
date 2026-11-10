/* datetime.h */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct { uint16_t year; uint8_t month, mday, hour, min, sec, wday; } datetime_t; /* wday 1=Mon..7=Sun */

uint8_t     bcd_to_bin(uint8_t bcd);
uint8_t     bin_to_bcd(uint8_t bin);
bool        bcd_is_valid(uint8_t bcd);
uint8_t     day_of_week(uint16_t y, uint8_t m, uint8_t d);
uint8_t     days_in_month(uint16_t y, uint8_t m);
bool        datetime_is_valid(const datetime_t *t);
const char *weekday_name(uint8_t wday);
bool        datetime_from_build(const char *date, const char *time, datetime_t *out);