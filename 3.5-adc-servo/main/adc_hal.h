#pragma once
#include <stdint.h>
#include <stdbool.h>

bool     hal_adc_init(int gpio);
bool  hal_adc_read_raw(int32_t *raw);
uint8_t hal_adc_resolution_bits(void);
uint32_t hal_adc_max_code(void);