#include "oled_cmds.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

static void put(uint8_t *out, size_t cap, size_t *n, uint8_t byte) {
    if (*n < cap) {
        out[*n] = byte;
    }
    *n += 1;
}

static void put2(uint8_t *out, size_t cap, size_t *n, uint8_t opcode, uint8_t param) {
    put(out, cap, n, opcode);
    put(out, cap, n, param);
}

void   oled_cfg_default(oled_cfg_t *cfg, uint8_t height) {
    cfg->height = height;
    cfg->rotate180 = false;
    cfg->contrast = 0x7F;
    cfg->addr_mode = OLED_ADDR_HORIZONTAL;
    cfg->pre_p1 = 1;
    cfg->pre_p2 = 15;
    cfg->vcomh = OLED_VCOMH_077;
    cfg->test_all_on = false;
}

size_t oled_build_init(const oled_cfg_t *cfg, uint8_t *out, size_t cap) {
    if (cfg == NULL || out == NULL) {
        return 0;
    }
    if (cfg->height != 32 && cfg->height != 64) {
        return 0;
    }
    if (cfg->pre_p1 < 1 || cfg->pre_p1 > 15) {
        return 0;
    }
    if (cfg->pre_p2 < 1 || cfg->pre_p2 > 15) {
        return 0;
    }

    if (cfg->addr_mode > 2) {
        return 0;
    }

    size_t n = 0;
    put(out, cap, &n, 0x00);
    put(out, cap, &n, 0xAE);
    put2(out, cap, &n, 0xA8, cfg->height - 1);
    put2(out, cap, &n, 0xD3, 0x00);
    put(out, cap, &n, 0x40);
    put2(out, cap, &n, 0x20, cfg->addr_mode);
    put(out, cap, &n, cfg->rotate180 ? 0xA0 : 0xA1);
    put(out, cap, &n, cfg->rotate180 ? 0xC0 : 0xC8);
    put2(out, cap, &n, 0xDA, (cfg->height == 64) ? 0x12 : 0x02);
    put2(out, cap, &n, 0x81, cfg->contrast);
    put2(out, cap, &n, 0xD9, (cfg->pre_p2 << 4) | cfg->pre_p1);
    put2(out, cap, &n, 0xDB, cfg->vcomh);
    put(out, cap, &n, 0xA4);
    put(out, cap, &n, 0xA6);
    put2(out, cap, &n, 0x8D, 0x14);
    put(out, cap, &n, 0xAF);

    if (cfg->test_all_on) {
        put(out, cap, &n, 0xA5);
    }

    return (n <= cap) ? n : 0;
}

