#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OLED_CTRL_CMD   0x00    // Co=0, D/C=0: following bytes are commands
#define OLED_CTRL_DATA  0x40    // Co=0, D/C=1: following bytes go to GDDRAM
#define OLED_INIT_MAX   32      // worst-case init packet length

typedef enum { OLED_ADDR_HORIZONTAL = 0, OLED_ADDR_VERTICAL = 1, OLED_ADDR_PAGE = 2 } oled_addr_mode_t;
typedef enum { OLED_VCOMH_065 = 0x00, OLED_VCOMH_077 = 0x20, OLED_VCOMH_083 = 0x30 } oled_vcomh_t;

typedef struct {
    uint8_t          height;        // 32 or 64
    bool             rotate180;     // A0/C0 instead of A1/C8
    uint8_t          contrast;      // 0..255
    oled_addr_mode_t addr_mode;
    uint8_t          pre_p1;        // 1..15 DCLK
    uint8_t          pre_p2;        // 1..15 DCLK
    oled_vcomh_t     vcomh;
    bool             test_all_on;   // append 0xA5
} oled_cfg_t;

void   oled_cfg_default(oled_cfg_t *cfg, uint8_t height);
size_t oled_build_init(const oled_cfg_t *cfg, uint8_t *out, size_t cap);   // 0 = bad cfg or buffer too small