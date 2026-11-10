#pragma once
#include <stdint.h>

#define FONT_W      5       /* columns per glyph */
#define FONT_H      7       /* rows used (bit 7 is spacing) */
#define FONT_FIRST  0x20    /* ' ' */
#define FONT_LAST   0x7E    /* '~' */

const uint8_t *font5x7_glyph(char c);   /* always returns a valid 5-byte glyph */