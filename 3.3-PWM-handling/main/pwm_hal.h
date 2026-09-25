#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HAL_PWM_CH_LED = 0,
    HAL_PWM_CH_MOTOR,
    HAL_PWM_CH_COUNT
} hal_pwm_ch_t;

bool hal_pwm_init(void);
bool hal_pwm_set_permille(hal_pwm_ch_t ch, uint16_t permille);   // 0..1000, clamps internally