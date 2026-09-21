#pragma once
#include <stdint.h>
#include <stdbool.h>

bool     hal_adc_init(void);
int      hal_adc_read_raw(void);          // returns raw code, or -1 on error
uint32_t hal_adc_raw_to_mv_manual(int raw);                 // formula-based
uint32_t hal_adc_raw_to_mv_calibrated(int raw);              // uses cal curve/table
uint8_t  hal_adc_resolution_bits(void);
uint32_t hal_adc_vref_mv(void);                              // for your reference table