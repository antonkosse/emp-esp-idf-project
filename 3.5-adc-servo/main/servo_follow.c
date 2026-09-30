#include "servo_follow.h"
#include <stdint.h>
#include <stdbool.h>

// bool servo_follow_init(s, cfg):
//     reject raw_at_0 == raw_at_180               // would divide by zero
//     reject alpha_q8 outside 1..256
//     copy cfg into s; ema_q8 = 0; last_deg = 0; started = false
//     return true

bool servo_follow_init(servo_follow_t *s, const servo_follow_cfg_t *cfg)
{
    if (cfg->raw_at_0 == cfg->raw_at_180)
    {
        return false;
    }
    if (cfg->alpha_q8 < 1 || cfg->alpha_q8 > SERVO_FOLLOW_Q8_ONE)
    {
        return false;
    }

    s->cfg = *cfg;
    s->ema_q8 = 0;
    s->last_deg = 0;
    s->started = false;

    return true;
}

// bool servo_follow_update(s, raw, *deg_out):
//     first = !started
//     x_q8 = raw * 256

//     // 1. filter
//     ema_q8 = first ? x_q8 : ema_q8 + (x_q8 - ema_q8) * alpha_q8 / 256

//     // 2. window (signed math throughout)
//     code = ema_q8 / 256
//     deg  = (code - raw_at_0) * 180 / (raw_at_180 - raw_at_0)
//     deg  = clamp(deg, 0, 180)

//     // 3. deadband
//     if first or |deg - last_deg| >= deadband_deg:
//         started = true; last_deg = deg; *deg_out = deg
//         return true
//     return false

static int32_t clamp(int32_t deg)
{
    if (deg < 0)
    {
        return 0;
    }
    if (deg > 180)
    {
        return 180;
    }
    return deg;
}

static inline int32_t abs_i32(int32_t x)
{
    return (x < 0) ? -x : x;
}

bool servo_follow_update(servo_follow_t *s, int32_t raw, int32_t *deg_out)
{
    bool first = !s->started;
    int32_t x_q8 = raw * SERVO_FOLLOW_Q8_ONE;
    s->ema_q8 = first ? x_q8 : s->ema_q8 + (x_q8 - s->ema_q8) * s->cfg.alpha_q8 / SERVO_FOLLOW_Q8_ONE;

    int32_t code = s->ema_q8 / SERVO_FOLLOW_Q8_ONE;

    const int32_t r0 = s->cfg.raw_at_0;
    const int32_t r1 = s->cfg.raw_at_180;

    int32_t deg = (code - r0) * 180 / (r1 - r0);
    deg = clamp(deg);

    if (first || abs_i32(deg - s->last_deg) >= s->cfg.deadband_deg)
    {
        s->started = true;
        s->last_deg = deg;
        *deg_out = deg;
        return true;
    }
    return false;
}

// uint32_t servo_follow_deg_to_us(const s, deg):
//     deg  = clamp(deg, 0, 180)
//     span = (int32) us_at_180 - (int32) us_at_0  // signed: handles reversed servo
//     return us_at_0 + deg * span / 180
uint32_t servo_follow_deg_to_us(const servo_follow_t *s, int32_t deg)
{
    deg = clamp(deg);
    int32_t span = (int32_t)s->cfg.us_at_180 - (int32_t)s->cfg.us_at_0;

    int32_t us = (int32_t)s->cfg.us_at_0 + deg * span / 180;
    return (uint32_t)us;
}
