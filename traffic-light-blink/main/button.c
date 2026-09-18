#include "button.h"
#include "traffic_light.h"
#include "traffic_light_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "button";

void button_task(void *pvParameters)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // idle = HIGH, pressed = LOW
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&btn_cfg);
    ESP_ERROR_CHECK(err);

    int last_stable = 1;
    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);
        if (level != last_stable) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (gpio_get_level(BUTTON_GPIO) == level && level == 0) {
                traffic_light_toggle_mode();
                ESP_LOGI(TAG, "Button pressed");
            }
            last_stable = level;
        }
        // debug
        // ESP_LOGI(TAG, "button raw level=%d last_stable=%d", level, last_stable);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}