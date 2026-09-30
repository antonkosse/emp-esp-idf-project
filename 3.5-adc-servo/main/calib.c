#include "calib.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "adc_hal.h"
#include "pwm_hal.h"

static const char *TAG = "calib";

#define POT_AVG_N         32
#define POT_REPORT_MS     250

#define SERVO_US_START    1500
#define SERVO_US_MIN      400
#define SERVO_US_MAX      2600
#define SERVO_STEP_FINE   5
#define SERVO_STEP_BIG    50

void calib_pot(void)
{
    ESP_LOGI(TAG, "Pot calibration. ADC max code = %" PRIu32, hal_adc_max_code());
    ESP_LOGI(TAG, "Set the knob on each mark and note 'avg'");

    for (;;) {
        int64_t sum = 0;
        int32_t lo = INT32_MAX;
        int32_t hi = INT32_MIN;
        int n = 0;

        for (int i = 0; i < POT_AVG_N; i++) {
            int32_t raw;
            if (hal_adc_read_raw(&raw)) {
                sum += raw;
                n++;
                if (raw < lo) lo = raw;
                if (raw > hi) hi = raw;
            }
        }

        if (n == 0) {
            ESP_LOGW(TAG, "no valid samples");
        } else {
            int32_t avg = (int32_t)((sum + n / 2) / n);
            ESP_LOGI(TAG, "avg=%4" PRId32 "  noise p-p=%3" PRId32 "  (%d samples)",
                     avg, hi - lo, n);
        }

        vTaskDelay(pdMS_TO_TICKS(POT_REPORT_MS));
    }
}

static void servo_help(void)
{
    ESP_LOGI(TAG, "keys: q=-50  w=-5  e=+5  r=+50  c=center  h=help");
}

void calib_servo(void)
{
    int32_t us = SERVO_US_START;

    setvbuf(stdin, NULL, _IONBF, 0);
    pwm_hw_set_pulse_us((uint32_t)us);
    ESP_LOGI(TAG, "Servo calibration. pulse = %" PRId32 " us", us);
    servo_help();

    for (;;) {
        int c = getchar();
        if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        switch (c) {
        case 'q': us -= SERVO_STEP_BIG;  break;
        case 'w': us -= SERVO_STEP_FINE; break;
        case 'e': us += SERVO_STEP_FINE; break;
        case 'r': us += SERVO_STEP_BIG;  break;
        case 'c': us = SERVO_US_START;   break;
        case 'h': servo_help();          continue;
        default:                         continue;
        }

        if (us < SERVO_US_MIN) us = SERVO_US_MIN;
        if (us > SERVO_US_MAX) us = SERVO_US_MAX;

        pwm_hw_set_pulse_us((uint32_t)us);
        ESP_LOGI(TAG, "pulse = %" PRId32 " us", us);
    }
}