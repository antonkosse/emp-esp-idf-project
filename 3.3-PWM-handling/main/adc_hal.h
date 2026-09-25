#pragma once
#include <stdint.h>
#include <stdbool.h>

bool     hal_adc_init(void);
int  hal_adc_read_raw(void);          // raw code, or -1 on error
uint8_t hal_adc_resolution_bits(void); // for normalizing raw -> 0..1000
uint32_t hal_adc_max_code(void);