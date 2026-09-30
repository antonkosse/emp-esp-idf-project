#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pwm_hal.h"
#include "adc_hal.h"
#include "calib.h"
#include "board_config.h"
#include "servo_follow.h"

static const char *TAG = "3.5-sg90";
static servo_follow_t s_follow; 

void setup(void)
{
    hal_adc_init(BOARD_POT_GPIO);
    pwm_hw_init(BOARD_SERVO_GPIO);

    servo_follow_cfg_t servo_cfg = {
        .raw_at_0 = BOARD_RAW_AT_0,
        .raw_at_180 = BOARD_RAW_AT_180,
        .us_at_0 = BOARD_US_AT_0,
        .us_at_180 = BOARD_US_AT_180,
        .alpha_q8 = BOARD_ALPHA_Q8,
        .deadband_deg = BOARD_DEADBAND_DEG};

    if (!servo_follow_init(&s_follow, &servo_cfg))
    {
        ESP_LOGE(TAG, "SERVO INIT FAILED");
    }
}

void app_main(void)
{
    setup();

    // loop forever:
    // if hal_adc_read_raw(&raw):
    //     if servo_follow_update(&s_follow, raw, &deg):
    //         pwm_hw_set_pulse_us(servo_follow_deg_to_us(&s_follow, deg))
    //         log("deviation from far-left = %d deg", deg)
    // vTaskDelay(ms_to_ticks(BOARD_LOOP_PERIOD_MS))
#if BOARD_CALIB_MODE == 1
    calib_pot();
#elif BOARD_CALIB_MODE == 2
    calib_servo();
#else
    for (;;)
    {
        int32_t raw = 0;
        int32_t deg = 0;
        if (hal_adc_read_raw(&raw))
        {
            if (servo_follow_update(&s_follow, raw, &deg)) {
                pwm_hw_set_pulse_us(servo_follow_deg_to_us(&s_follow, deg));
                ESP_LOGI(TAG, "deviation from far-left = %d deg", deg);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BOARD_LOOP_PERIOD_MS));
    }
#endif
}