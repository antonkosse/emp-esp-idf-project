#include <stdbool.h>
#include "driver/gpio.h"
#include "traffic_light.h"
#include "traffic_light_config.h"
#include "pedestrian_traffic_light.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "pedestrian_traffic_light";
static pedestrian_light_pins_t pedestrian_s_pins;
static traffic_light_state_t s_state;
static traffic_light_state_t previous_state;

static void set_lights(bool red, bool green)
{
    gpio_set_level(pedestrian_s_pins.pedestrian_red_light, red);
    gpio_set_level(pedestrian_s_pins.pedestrian_green_light, green);
}

void pedestrian_traffic_light_init(const pedestrian_light_pins_t *pins)
{
    pedestrian_s_pins = *pins;
    uint64_t mask = (1ULL << pins->pedestrian_red_light) | (1ULL << pins->pedestrian_green_light);
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    ESP_ERROR_CHECK(err);
    set_lights(false, false);
}

void pedestrian_traffic_light_run_task(void *pvParameters)
{
    while (1) {
        s_state = traffic_light_get_state();
        ESP_LOGI(TAG, "car_state=%d red=%d green=%d", s_state,
         gpio_get_level(pedestrian_s_pins.pedestrian_red_light),
         gpio_get_level(pedestrian_s_pins.pedestrian_green_light));
        if (s_state == TL_STATE_GREEN || s_state == TL_STATE_GREEN_FLASHING || s_state == TL_STATE_YELLOW) {
            if (previous_state != s_state) {
                ESP_LOGI(TAG, "RED PEDESTRIAN");
                int8_t level = gpio_get_level(pedestrian_s_pins.pedestrian_red_light);
                ESP_LOGI(TAG, "Red level: %d", level);
            }
             set_lights(true, false);
        }

        if (s_state == TL_STATE_RED || s_state == TL_STATE_RED_YELLOW) {
             if (previous_state != s_state) {
                ESP_LOGI(TAG, "GREEN PEDESTRIAN");
                int8_t level = gpio_get_level(pedestrian_s_pins.pedestrian_green_light);
                ESP_LOGI(TAG, "Green level: %d", level);
             }
             set_lights(false, true);
        }

        if (s_state == TL_STATE_FLASHING_YELLOW) {
            // turn off pedestrian
            if (previous_state != s_state) {
               ESP_LOGI(TAG, "PEDESTRIAN TURNED OF");
            }
            set_lights(false, false);
        }
        if (previous_state != s_state) {
            previous_state = s_state;
        }
        vTaskDelay(pdMS_TO_TICKS(TL_PEDESTRIAN_LIGHT_POLL_DURATION_MS));
    }
}