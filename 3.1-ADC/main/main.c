/**
 * Lesson 3.1 — ADC oneshot: raw + U_adc + LED за порогом
 * Схема: 3.3V — R1 10k — ● — LDR — GND; ● → GPIO4
 * GPIO41 —[220 Ω]— LED+ ) LED− — GND
 *
 * LDR внизу: світло ↑ → raw ↓. LED ON, коли raw < RAW_LED_ON (світло).
 */
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sma_filter.h"
static const char *TAG = "3.1-adc";
/* ESP32-S3: GPIO4 = ADC1_CH3 */
#define ADC_GPIO GPIO_NUM_4
#define LED_GPIO GPIO_NUM_41
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_12
#define SAMPLE_PERIOD_MS 200
#define U_FS_VOLTS 3.3f
#define ADC_MAX_CODE 4095
/* Підлаштувати під своє освітлення після першого логу raw */
#define RAW_LED_ON 2000
static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channel;
static sma_filter_t s_filter; 

static void setup_sma(void) {
    sma_filter_init(&s_filter);
}

static void setup_led(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
}
static void setup_adc(void)
{
    adc_unit_t unit = 0;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ADC_GPIO, &unit, &s_channel));
    adc_oneshot_unit_init_cfg_t init = {
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init, &s_adc));
    adc_oneshot_chan_cfg_t ch = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_channel, &ch));
    ESP_LOGI(TAG, "GPIO%d -> ADC%d_CH%d", (int)ADC_GPIO, (int)unit + 1, (int)s_channel);
}
void app_main(void)
{
    setup_led();
    setup_adc();
    setup_sma();
    ESP_LOGI(TAG, "oneshot 12-bit atten=12dB; U ≈ raw * %.1f / %d",
             U_FS_VOLTS, ADC_MAX_CODE);
    ESP_LOGI(TAG, "LED GPIO%d ON if raw < %d (light)", (int)LED_GPIO, RAW_LED_ON);
    ESP_LOGI(TAG, "Compare Serial U_adc with multimeter at GPIO%d node", (int)ADC_GPIO);
    while (1)
    {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, s_channel, &raw));
        const float u_adc = (float)raw * (U_FS_VOLTS / (float)ADC_MAX_CODE);
        const int filtered = sma_filter_update(&s_filter, raw);
        const int led_on = (filtered < RAW_LED_ON) ? 1 : 0;
        gpio_set_level(LED_GPIO, led_on);
        ESP_LOGI(TAG, "raw=%4d filtered=%4d U_adc=%.3f V led=%s",
                 raw, filtered, u_adc, led_on ? "ON" : "OFF");
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}