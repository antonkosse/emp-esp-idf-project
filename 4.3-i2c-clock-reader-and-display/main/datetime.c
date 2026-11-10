#include "datetime.h"
#include <string.h>

#define YEAR_MIN 2000
#define YEAR_MAX 2099
#define BUILD_DATE_LEN 11
#define BUILD_TIME_LEN 8

static const uint8_t DAYS_IN_MONTH[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
static const uint8_t MONTH_OFFSET_MOD7[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
static const char *const WEEKDAY_NAMES[8] = {"???", "Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
static const char* MONTH_ABBR = "JanFebMarAprMayJunJulAugSepOctNovDec";

static bool is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool two_digits(const char *p, uint8_t *value) {
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') {
        return false;
    }

    *value = (p[0] - '0') * 10 + (p[1] - '0');

    return true;
}

uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

uint8_t bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

bool bcd_is_valid(uint8_t bcd) {
    return ((bcd >> 4) <= 9) && ((bcd & 0x0F) <= 9);
}

uint8_t day_of_week(uint16_t y, uint8_t m, uint8_t d) {
    if (m < 1 || m > 12) {
        return 0;
    }

    int yy = y;

    if (m < 3) {
        yy--;
    }

    uint8_t r = (yy + yy/4 - yy/100 + yy/400 + MONTH_OFFSET_MOD7[m-1] + d) % 7;

    return (r == 0) ? 7 : r;
}

uint8_t days_in_month(uint16_t y, uint8_t m) {
    if (m < 1 || m > 12) {
        return 0;
    }

    if (m == 2 && is_leap_year(y)) {
        return 29;
    }

    return DAYS_IN_MONTH[m-1];
}

bool datetime_is_valid(const datetime_t *t) {
    if (t == NULL) {
        return false;
    }

    return YEAR_MIN <= t->year && t->year <= YEAR_MAX
    && t->month >= 1 && t->month  <=12
    && t->mday >= 1 && t->mday <= days_in_month(t->year, t->month)
    && t->hour <= 23 && t->min <= 59 && t->sec <= 59
    && t->wday >= 1 && t->wday <= 7;
}

const char* weekday_name(uint8_t wday) {
    if (wday >= 1 && wday <= 7) {
        return WEEKDAY_NAMES[wday];
    }

    return WEEKDAY_NAMES[0];
}

bool datetime_from_build(const char *date, const char *time, datetime_t *out) {
    if (date == NULL || time == NULL || out == NULL) {
        return false;
    }

    if (strlen(date) != BUILD_DATE_LEN || strlen(time) != BUILD_TIME_LEN) {
        return false;
    }

    uint8_t m = 0;

    for (uint8_t i = 0; i < 12; i++) {
        if (memcmp(date, MONTH_ABBR + 3*i, 3) == 0) {
            m = i+1;
            break;
        }
    }

    if (m == 0) {
        return false;
    }

    char    day_txt[2];
    uint8_t day, hi, lo, hour, min, sec;

    /* __DATE__ pads the day with a space: " 2" -> "02" in a local copy */
    day_txt[0] = (date[4] == ' ') ? '0' : date[4];
    day_txt[1] = date[5];
    if (!two_digits(day_txt, &day)) {
        return false;
    }

    if (!two_digits(date + 7, &hi) || !two_digits(date + 9, &lo)) {
        return false;
    }
    uint16_t year = (uint16_t)(hi * 100u + lo);

    if (time[2] != ':' || time[5] != ':') {
        return false;
    }
    if (!two_digits(time + 0, &hour) ||
        !two_digits(time + 3, &min)  ||
        !two_digits(time + 6, &sec)) {
        return false;
    }

    *out = (datetime_t){
        .year = year,
        .month = m,
        .mday = day,
        .hour = hour,
        .min = min,
        .sec = sec
    };

    out->wday = day_of_week(year, m, day);

    return datetime_is_valid(out);

}