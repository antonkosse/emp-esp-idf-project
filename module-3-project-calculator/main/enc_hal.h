// enc_hal.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

bool    enc_hal_init(void);
int32_t enc_hal_read_steps(void);