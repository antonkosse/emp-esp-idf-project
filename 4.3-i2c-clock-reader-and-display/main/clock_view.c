#include "clock_view.h"
#include "datetime.h"
#include "oled.h"
#include "font5x7.h"
#include <stdio.h>
#include <string.h>

#define GLYPH_PITCH (FONT_W + 1)
#define TIME_Y 16
#define TIME_SCALE 2
#define DATE_Y 40
#define DATE_SCALE 1
#define MSG_Y 24
#define MSG_SCALE 1
#define TIME_BUF 12
#define DATE_BUF 20

static const char TIME_STR[] = "--:--:--";
static const char DATE_STR[] = "--- --.--.----";

static int center_x(int len, int scale) {
    int width = len * GLYPH_PITCH * scale - scale;
    if (width >= OLED_WIDTH) {
        return 0;
    }
    return (OLED_WIDTH - width) / 2;
}

static void draw_centered(int y, const char* text, int scale) {
    int x = center_x(strlen(text), scale);

    oled_text_scaled(x, y, text, scale);
}

void clock_view_draw(const datetime_t *t) {
    oled_fb_clear();
    char time_txt[TIME_BUF], date_txt[DATE_BUF];

    if (t == NULL || !datetime_is_valid(t)) {
        draw_centered(TIME_Y, TIME_STR, TIME_SCALE);
        draw_centered(DATE_Y, DATE_STR, DATE_SCALE);
    } else {
        snprintf(time_txt, sizeof(time_txt), "%02u:%02u:%02u",
                 (unsigned)t->hour, (unsigned)t->min, (unsigned)t->sec);
        snprintf(date_txt, sizeof(date_txt), "%s %02u.%02u.%04u",
                 weekday_name(t->wday), (unsigned)t->mday, (unsigned)t->month, (unsigned)t->year);
        draw_centered(TIME_Y, time_txt, TIME_SCALE);
        draw_centered(DATE_Y, date_txt, DATE_SCALE);
    }
}

void clock_view_message(const char *msg) {
    oled_fb_clear();
    if (msg == NULL) {
        return;
    }

    draw_centered(MSG_Y, msg, MSG_SCALE);
}