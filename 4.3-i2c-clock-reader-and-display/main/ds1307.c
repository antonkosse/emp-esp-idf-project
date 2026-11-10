#include "ds1307.h"
#include "datetime.h"
#include "i2c_hal.h"

#define RAW_LEN 7
// 1000 0000
#define SEC_CH 0x80
// 0111 1111
#define SEC_MASK 0x7F
// 0111 1111
#define MIN_MASK 0x7F
// 0100 0000
#define HOUR_12H 0x40
// 0010 0000
#define HOUR_PM 0x20
// 0011 1111
#define HOUR_MASK_24 0x3F
// 0001 1111
#define HOUR_MASK_12 0x1F
// 0011 1111
#define DATE_MASK 0x3F
// 0001 1111
#define MONTH_MASK 0x1F
#define YEAR_BASE 2000

static i2c_status_t read_regs(uint8_t reg, uint8_t *rd, size_t rlen) {
    return i2c_hal_write_read(DS1307_ADDR, &reg, 1, rd, rlen);
}

static bool bcd_field(uint8_t byte, uint8_t mask, uint8_t *value) {
    uint8_t v = byte & mask;
    if (!bcd_is_valid(v)) {
        return false;
    }

    *value = bcd_to_bin(v);
    return true;
}

bool ds1307_decode(const uint8_t raw[7], datetime_t *out) {
    if (raw == NULL || out == NULL) {
        return false;
    }
    uint8_t sec;
    uint8_t min;
    uint8_t h12;
    uint8_t hour;
    uint8_t month;
    uint8_t mday;
    uint8_t yy;
    
    if (!bcd_field(raw[0], SEC_MASK, &sec)) {
        return false;
    }
    if (!bcd_field(raw[1], MIN_MASK, &min)) {
        return false;
    }

    if (raw[2] & HOUR_12H) {
        if (!bcd_field(raw[2], HOUR_MASK_12, &h12)) {
            return false;
        }

        if (h12 < 1 || h12 > 12) {
            return false;
        }

        hour = (h12 % 12) + ((raw[2] & HOUR_PM) ? 12 : 0);
    } else {
        if (!bcd_field(raw[2], HOUR_MASK_24, &hour)) {
            return false;
        }
    }

    if (!bcd_field(raw[4], DATE_MASK, &mday)) {
        return false;
    }
    if (!bcd_field(raw[5], MONTH_MASK, &month)) {
        return false;
    }
    
    if (!bcd_field(raw[6], 0xFF, &yy)) {
        return false;
    }

    out->year = YEAR_BASE + yy;
    out->month = month;
    out->mday = mday;
    out->hour = hour;
    out->min = min;
    out->sec = sec;
    
    out->wday = day_of_week(out->year, month, mday);

    return datetime_is_valid(out);
}

void ds1307_encode(const datetime_t *in, uint8_t raw[7]) {
    raw[0] = bin_to_bcd(in->sec);
    raw[1] = bin_to_bcd(in->min);
    raw[2] = bin_to_bcd(in->hour);
    raw[3] = day_of_week(in->year, in->month, in->mday);
    raw[4] = bin_to_bcd(in->mday);
    raw[5] = bin_to_bcd(in->month);
    raw[6] = bin_to_bcd(in->year - YEAR_BASE);
}

ds1307_status_t ds1307_init(void) {
    uint8_t sec_reg;
    if (i2c_hal_probe(DS1307_ADDR) != I2C_OK) {
        return DS1307_BUS_ERR;
    }

    if (!i2c_hal_add(DS1307_ADDR)) {
        return DS1307_BUS_ERR;
    }

    if (read_regs(DS1307_REG_SECONDS, &sec_reg, 1) != I2C_OK) {
        return DS1307_BUS_ERR;
    }

    if (sec_reg & SEC_CH) {
        return DS1307_HALTED;
    }

    return DS1307_OK;
}

ds1307_status_t ds1307_read(datetime_t *out) {
    uint8_t raw[RAW_LEN];
    if (out == NULL) {
        return DS1307_BAD_DATA;
    }

    if (read_regs(DS1307_REG_SECONDS, raw, RAW_LEN) != I2C_OK) {
        return DS1307_BUS_ERR;
    }

    if (raw[0] & SEC_CH) {
        return DS1307_HALTED;
    }

    if (!ds1307_decode(raw, out)) {
        return DS1307_BAD_DATA;
    }

    return DS1307_OK;
}

ds1307_status_t ds1307_write(const datetime_t *in) {
    if (!datetime_is_valid(in)) {
        return DS1307_BAD_DATA;
    }

    uint8_t pkt[RAW_LEN +1];
    pkt[0] = DS1307_REG_SECONDS;

    ds1307_encode(in, pkt + 1);
    if (i2c_hal_write(DS1307_ADDR, pkt, RAW_LEN + 1) != I2C_OK) {
        return DS1307_BUS_ERR;
    }

    return DS1307_OK;
}
