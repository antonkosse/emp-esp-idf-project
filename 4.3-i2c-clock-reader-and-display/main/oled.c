#include "oled.h"

#include <string.h>

#include "font5x7.h"
#include "i2c_hal.h"
#include "oled_cmds.h"

#define OLED_MAX_PAGES      8
#define FB_SIZE             (OLED_WIDTH * OLED_MAX_PAGES)
#define GLYPH_PITCH         (FONT_W + 1)

#define SSD1306_COL_ADDR    0x21
#define SSD1306_PAGE_ADDR   0x22

/* s_buf[0] is the I2C control byte, the framebuffer follows it,
   so a whole frame goes out in one transfer without copying. */
static uint8_t        s_buf[1 + FB_SIZE];
static uint8_t *const s_fb = s_buf + 1;

static uint8_t s_addr;
static uint8_t s_height;
static uint8_t s_pages;
static bool    s_ok;

bool oled_init(uint8_t addr7, uint8_t height)
{
    s_ok = false;

    if (height != 32 && height != 64) {
        return false;
    }
    if (!i2c_hal_add(addr7)) {
        return false;
    }

    s_addr = addr7;
    s_height = height;
    s_pages = height / 8;

    oled_cfg_t cfg;
    oled_cfg_default(&cfg, height);
    cfg.addr_mode = OLED_ADDR_HORIZONTAL;   /* oled_flush depends on it */

    uint8_t cmd[OLED_INIT_MAX];
    size_t n = oled_build_init(&cfg, cmd, sizeof cmd);
    if (n == 0) {
        return false;
    }
    if (i2c_hal_write(s_addr, cmd, n) != I2C_OK) {
        return false;
    }

    s_ok = true;
    oled_fb_clear();
    return oled_flush();                    /* replace power-up snow with black */
}

void oled_fb_clear(void)
{
    memset(s_fb, 0, FB_SIZE);
}

void oled_pixel(int x, int y, bool on)
{
    /* unsigned cast also rejects negative coordinates */
    if ((unsigned)x >= OLED_WIDTH || (unsigned)y >= s_height) {
        return;
    }

    uint8_t *p = &s_fb[(y >> 3) * OLED_WIDTH + x];
    uint8_t mask = (uint8_t)(1u << (y & 7));

    if (on) {
        *p |= mask;
    } else {
        *p &= (uint8_t)~mask;
    }
}

void oled_fill(int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            oled_pixel(x + i, y + j, on);
        }
    }
}

void oled_text_xy(int x, int y, const char *s)
{
    if (s == NULL) {
        return;
    }

    for (; *s != '\0'; s++, x += GLYPH_PITCH) {
        const uint8_t *g = font5x7_glyph(*s);

        for (int col = 0; col < FONT_W; col++) {
            for (int row = 0; row < 8; row++) {
                oled_pixel(x + col, y + row, (g[col] >> row) & 1u);
            }
        }
    }
}

void oled_text_scaled(int x, int y, const char *s, int scale)
{
    if (s == NULL) {
        return;
    }
    if (scale <= 1) {
        oled_text_xy(x, y, s);
        return;
    }

    for (; *s != '\0'; s++, x += GLYPH_PITCH * scale) {
        const uint8_t *g = font5x7_glyph(*s);

        for (int col = 0; col < FONT_W; col++) {
            for (int row = 0; row < 8; row++) {
                oled_fill(x + col * scale, y + row * scale, scale, scale,
                          (g[col] >> row) & 1u);
            }
        }
    }
}

bool oled_flush(void)
{
    if (!s_ok) {
        return false;
    }

    /* re-anchor the RAM pointer every frame: column 0..127, page 0..pages-1 */
    const uint8_t win[] = {
        OLED_CTRL_CMD,
        SSD1306_COL_ADDR, 0x00, OLED_WIDTH - 1,
        SSD1306_PAGE_ADDR, 0x00, (uint8_t)(s_pages - 1),
    };
    if (i2c_hal_write(s_addr, win, sizeof win) != I2C_OK) {
        return false;
    }

    s_buf[0] = OLED_CTRL_DATA;
    return i2c_hal_write(s_addr, s_buf, 1 + (size_t)OLED_WIDTH * s_pages) == I2C_OK;
}