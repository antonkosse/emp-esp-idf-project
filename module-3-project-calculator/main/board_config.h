// board_config.h -- the only file that knows the wiring
// encoder module has 10k pull-ups on CLK, DT, SW
#pragma once

#define ENC_PIN_A           6       // CLK
#define ENC_PIN_B           7       // DT
#define BTN_PIN             8       // SW
#define BTN_ACTIVE_LEVEL    0       // active low: pressed reads 0
#define ENC_COUNTS_PER_STEP 4       // measured: raw counts per click
#define ENC_GLITCH_NS       1000    // PCNT glitch filter
#define TICK_MS             10       // main loop period
#define BTN_DEBOUNCE_MS     20 

_Static_assert(BTN_DEBOUNCE_MS >= TICK_MS,
               "debounce window must be at least one tick");
_Static_assert(BTN_DEBOUNCE_MS / TICK_MS <= 255,
               "threshold must fit in uint8_t");