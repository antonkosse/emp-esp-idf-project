
#pragma once
#include <stdint.h>

bool  buzzer_hal_init(void);
void buzzer_hal_set_tone(uint32_t freq_hz);