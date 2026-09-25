#include "pwm_hal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define MOTOR_GPIO GPIO_NUM_5
#define LED_GPIO GPIO_NUM_18
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_RES LEDC_TIMER_8_BIT

static const char *TAG = "3.3-pwm-hal";

static const int k_gpio[HAL_PWM_CH_COUNT] = {LED_GPIO, MOTOR_GPIO};
static const int k_timer[HAL_PWM_CH_COUNT] = {LEDC_TIMER_0, LEDC_TIMER_1};
static const int k_freq[HAL_PWM_CH_COUNT] = {5000, 2000};
static const ledc_channel_t k_channel[HAL_PWM_CH_COUNT] = {
    LEDC_CHANNEL_0, // HAL_PWM_CH_LED
    LEDC_CHANNEL_1, // HAL_PWM_CH_MOTOR
};
static const ledc_timer_bit_t k_res[HAL_PWM_CH_COUNT] = {
    LEDC_TIMER_13_BIT,
    LEDC_TIMER_8_BIT,
};

bool hal_pwm_init(void)
{
    for (hal_pwm_ch_t ch = 0; ch < HAL_PWM_CH_COUNT; ch++)
    {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = PWM_MODE,
            .duty_resolution = k_res[ch],
            .timer_num = k_timer[ch],
            .freq_hz = k_freq[ch],
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
        ledc_channel_config_t ledc_ch = {
            .gpio_num = k_gpio[ch],
            .speed_mode = PWM_MODE,
            .channel = k_channel[ch],
            .timer_sel = k_timer[ch],
            .duty = 0,
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE,
            .flags.output_invert = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch));
    }
     return true;
}

bool hal_pwm_set_permille(hal_pwm_ch_t ch, uint16_t permille) {
    if (ch >= HAL_PWM_CH_COUNT) {
        return false;
    }
    uint32_t max_duty = (1u << PWM_RES) - 1; 
    uint32_t duty = (uint32_t)permille * max_duty / 1000;

    esp_err_t err = ledc_set_duty(PWM_MODE, k_channel[ch], duty);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_set_duty failed: %s", esp_err_to_name(err));
        return false;
    }
    err = ledc_update_duty(PWM_MODE, k_channel[ch]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_update_duty failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}