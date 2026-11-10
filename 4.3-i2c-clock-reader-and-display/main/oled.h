#pragma once
#include <stdbool.h>
#include <stdint.h>

#define OLED_WIDTH 128

bool oled_init(uint8_t addr7, uint8_t height);
void oled_fb_clear(void);
void oled_pixel(int x, int y, bool on);
void oled_fill(int x, int y, int w, int h, bool on);
void oled_text_xy(int x, int y, const char *s);
void oled_text_scaled(int x, int y, const char *s, int scale);
bool oled_flush(void);