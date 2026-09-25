#include "adc_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

#define ADC_GPIO GPIO_NUM_4
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_12
#define ADC_MAX_CODE ((1 << ADC_BITWIDTH_12) - 1)

static const char *TAG = "3.3-adc";

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channel;

bool hal_adc_init(void)
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
    return true;
}

int hal_adc_read_raw(void)
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, s_channel, &raw);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "adc read failed: %s", esp_err_to_name(err));
        return -1;
    }
    return raw;
}

uint8_t hal_adc_resolution_bits(void) {
    return (uint8_t)ADC_BITWIDTH;   // ADC_BITWIDTH_12 == 12
}

uint32_t hal_adc_max_code(void) {
    return (uint32_t) ADC_MAX_CODE;
}