/* servo_follow.h */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SERVO_FOLLOW_Q8_SHIFT   8
/* Requires ADC_BITS + 2 * Q8_SHIFT <= 30 for int32 math */
#define SERVO_FOLLOW_Q8_ONE     (1 << SERVO_FOLLOW_Q8_SHIFT) 

typedef struct {                /* 1. settings, filled by main.c from board_config.h */
    int32_t  raw_at_0, raw_at_180;
    int32_t  alpha_q8, deadband_deg;
    uint32_t us_at_0, us_at_180;
} servo_follow_cfg_t;

typedef struct {                /* 2. runtime state, owned by the caller */
    servo_follow_cfg_t cfg;
    int32_t ema_q8;
    int32_t last_deg;
    bool    started;
} servo_follow_t;

/* 3. operations */
bool     servo_follow_init(servo_follow_t *s, const servo_follow_cfg_t *cfg);
bool     servo_follow_update(servo_follow_t *s, int32_t raw, int32_t *deg_out);
uint32_t servo_follow_deg_to_us(const servo_follow_t *s, int32_t deg);