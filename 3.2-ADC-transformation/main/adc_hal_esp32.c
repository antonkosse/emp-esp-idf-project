#include "adc_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

#define ADC_GPIO GPIO_NUM_4
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_12
#define SAMPLE_PERIOD_MS 100
#define U_FS_VOLTS 3.3f
#define ADC_MAX_CODE ((1 << ADC_BITWIDTH_12) - 1)


static const char *TAG = "3.2-adc-transform";
static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channel;
static adc_cali_handle_t s_cali_handle;
static bool s_cali_ready;

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
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_channel, &ch));
    ESP_LOGI(TAG, "GPIO%d -> ADC%d_CH%d", (int)ADC_GPIO, (int)unit + 1, (int)s_channel);
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,          // same unit resolved in setup_adc()
        .atten = ADC_ATTEN,       // same constant you already defined
        .bitwidth = ADC_BITWIDTH, // same constant you already defined
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    s_cali_ready = (err == ESP_OK);
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

uint32_t hal_adc_raw_to_mv_manual(int raw)
{
    float u_volts = (float)raw * (U_FS_VOLTS / (float)ADC_MAX_CODE);
    return (uint32_t)(u_volts * 1000.0f); // rounding
}

uint32_t hal_adc_raw_to_mv_calibrated(int raw)
{
    if (!s_cali_ready)
    {
        return 0;
    }
    int mv = 0;
    esp_err_t err = adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "cali convert failed: %s", esp_err_to_name(err));
        return 0;
    }
    return (uint32_t)mv;
}

uint8_t hal_adc_resolution_bits(void) {
    return (uint8_t)ADC_BITWIDTH;   // ADC_BITWIDTH_12 == 12
}

uint32_t hal_adc_vref_mv(void) {
    return (uint32_t)(U_FS_VOLTS * 1000.0f);
}