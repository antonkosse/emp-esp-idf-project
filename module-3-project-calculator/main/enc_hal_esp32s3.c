#include "enc_hal.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_err.h"

static pcnt_unit_handle_t s_pcnt;
static int s_last = 0;
static int s_acc = 0;

bool enc_hal_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 32767,
        .low_limit = -32768,
        .flags.accum_count = true,
    };
    esp_err_t err = pcnt_new_unit(&unit_cfg, &s_pcnt);
    if (err != ESP_OK)
    {
        return false;
    }
    pcnt_unit_add_watch_point(s_pcnt, unit_cfg.high_limit);
    pcnt_chan_config_t ch_a_cfg = {
        .edge_gpio_num = ENC_PIN_A,
        .level_gpio_num = ENC_PIN_B,
    };
    pcnt_channel_handle_t ch_a;
    err = pcnt_new_channel(s_pcnt, &ch_a_cfg, &ch_a);
    if (err != ESP_OK)
    {
        return false;
    }
    err = pcnt_channel_set_edge_action(
        ch_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK)
    {
        return false;
    }
    err = pcnt_channel_set_level_action(
        ch_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK)
    {
        return false;
    }
    pcnt_chan_config_t ch_b_cfg = {
        .edge_gpio_num = ENC_PIN_B,
        .level_gpio_num = ENC_PIN_A,
    };
    pcnt_channel_handle_t ch_b;
    err = pcnt_new_channel(s_pcnt, &ch_b_cfg, &ch_b);
    if (err != ESP_OK)
    {
        return false;
    }
    err = pcnt_channel_set_edge_action(
        ch_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (err != ESP_OK)
    {
        return false;
    }
    err = pcnt_channel_set_level_action(
        ch_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    if (err != ESP_OK)
    {
        return false;
    }
    pcnt_glitch_filter_config_t filt_cfg = {
        .max_glitch_ns = ENC_GLITCH_NS, // pulses shorter than this are ignored
    };
    err = pcnt_unit_set_glitch_filter(s_pcnt, &filt_cfg);
    if (err != ESP_OK)
    {
        return false;
    }
    pcnt_unit_add_watch_point(s_pcnt, unit_cfg.low_limit);
    err = pcnt_unit_enable(s_pcnt);

    if (err != ESP_OK)
    {
        return false;
    }
    err = pcnt_unit_clear_count(s_pcnt);

    if (err != ESP_OK)
    {
        return false;
    }
    err = pcnt_unit_start(s_pcnt);

    if (err != ESP_OK)
    {
        return false;
    }
    s_last = 0;
    s_acc = 0;
    return true;
}

int32_t enc_hal_read_steps(void) {
    int now;
    esp_err_t err = pcnt_unit_get_count(s_pcnt, &now);

    if (err != ESP_OK) {
        return 0;  // treat as "no movement"
    }
    int delta = now - s_last;
    s_last = now;
    s_acc += delta;

    int32_t steps = (int32_t) (s_acc / ENC_COUNTS_PER_STEP);
    s_acc -= steps * ENC_COUNTS_PER_STEP;

    return steps;
}