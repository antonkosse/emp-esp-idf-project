#include "buzzer_hal.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "buzzer_ledc";

#define BUZZER_GPIO GPIO_NUM_18
#define LEDC_MODE LEDC_LOW_SPEED_MODE // S3 has ONLY low-speed mode, no high-speed group
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_RES_BITS LEDC_TIMER_10_BIT // duty resolution, trade-off vs max achievable freq
#define DUTY_50PCT ((1 << LEDC_RES_BITS) / 2)

static bool buzzer_set_freq_safe(uint32_t freq_hz)
{
    esp_err_t err = ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_freq(%lu Hz) failed: %s", (unsigned long)freq_hz, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool buzzer_hal_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RES_BITS,
        .timer_num = LEDC_TIMER,
        .freq_hz = 440,
        .clk_cfg = LEDC_AUTO_CLK};

    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE,
        .flags.output_invert = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    return true;
}

void buzzer_hal_set_tone(uint32_t freq_hz)
{
    if (freq_hz == 0 || !buzzer_set_freq_safe(freq_hz))
    {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        return;
    }
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, DUTY_50PCT);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}