#include <stdio.h>
#include "adc_hal.h"
#include "pwm_hal.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAMPLE_PERIOD_MS 500
#define LED_DEADZONE 30
#define MOTOR_DEADZONE 250
#define MOTOR_START 650
#define MOTOR_SPAN_IN (1000 - MOTOR_DEADZONE)
#define MOTOR_SPAN_OUT (1000 - MOTOR_START)

static const char *TAG = "3.3-tt";

static void setup(void)
{
    hal_adc_init();
    hal_pwm_init();
}

static uint16_t map_led(uint16_t permille)
{
    if (permille < LED_DEADZONE) {
        return 0;
    }
    return permille;
}

static uint16_t map_motor(uint16_t permille)
{
    if (permille < MOTOR_DEADZONE) {
        return 0;
    }
    uint16_t scaled = ((permille - MOTOR_DEADZONE) * MOTOR_SPAN_OUT) / MOTOR_SPAN_IN; 
    return MOTOR_START + scaled;
}

void app_main(void)
{
    setup();
    for (;;)
    {
        int raw = hal_adc_read_raw();

        if (raw < 0)
        {
            ESP_LOGW(TAG, "skipping sample: raw read failed");
        }
        else
        {
            int adc_max = (1 << hal_adc_resolution_bits()) - 1;
            uint16_t x = (raw * 1000) / adc_max;
            uint16_t led_permille = map_led(x);
            uint16_t motor_permille = map_motor(x);
            if (!hal_pwm_set_permille(HAL_PWM_CH_LED, led_permille))
            {
                ESP_LOGW(TAG, "failed to set LED duty (permille=%u)", led_permille);
            }
            if (!hal_pwm_set_permille(HAL_PWM_CH_MOTOR, motor_permille))
            {
                ESP_LOGW(TAG, "failed to set motor duty (permille=%u)", motor_permille);
            }
            ESP_LOGI(TAG, "raw=%d led_permille=%u motor_permille=%u", raw, led_permille, motor_permille);
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}