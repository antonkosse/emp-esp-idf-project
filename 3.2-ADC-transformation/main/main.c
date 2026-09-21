#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "adc_hal.h"

#define SAMPLE_PERIOD_MS 500

static const char *TAG = "3.2-adc-transform";
/* ESP32-S3: GPIO4 = ADC1_CH3 */


static void print_table_header(void)
{
    printf("Resolution : %u-bit\n", hal_adc_resolution_bits());
    printf("Vref(FS)   : %lu mV\n", hal_adc_vref_mv());
    printf("\n");
    printf("RAW   U_manual(mV)  U_cali(mV)  Error(%%)\n");
    printf("------------------------------------------\n");
}

static void print_row(int raw, uint32_t u_manual, uint32_t u_cali, float error_pct)
{
    printf("%-6d%-14lu%-12lu%.2f\n", raw, u_manual, u_cali, error_pct);
}

void app_main(void)
{
    hal_adc_init();
    print_table_header();
    uint32_t row_count = 0;   // plain local — fine, since app_main never returns


    while (1) {
        int raw = hal_adc_read_raw();

        if (raw < 0) {
            ESP_LOGW(TAG, "skipping sample: raw read failed");
        } else {
            uint32_t u_manual = hal_adc_raw_to_mv_manual(raw);
            uint32_t u_cali   = hal_adc_raw_to_mv_calibrated(raw);

            if (u_cali == 0) {
                ESP_LOGW(TAG, "skipping sample: u_cali=0, error%% undefined");
            } else {
                float error_pct = fabsf((float)u_manual - (float)u_cali)
                                   / (float)u_cali * 100.0f;
                if (row_count > 0 && row_count % 10 == 0) {
                    print_table_header();
                }

                print_row(raw, u_manual, u_cali, error_pct);
                row_count++;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}
