#include <stdatomic.h>
#include "traffic_light.h"
#include "traffic_light_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "traffic_light";
static traffic_light_pins_t s_pins;
static _Atomic traffic_light_state_t s_state = TL_STATE_RED;
static _Atomic traffic_light_mode_t s_mode = TL_MODE_NORMAL;

static void set_lights(bool red, bool yellow, bool green)
{
    gpio_set_level(s_pins.red_pin, red);
    gpio_set_level(s_pins.yellow_pin, yellow);
    gpio_set_level(s_pins.green_pin, green);
}

// Waits in small chunks, bailing out early if the mode changes mid-wait.
static bool wait_or_mode_change(uint32_t total_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < total_ms) {
        uint32_t step = (total_ms - elapsed) < TL_BLINK_PERIOD_MS ? (total_ms - elapsed) : TL_BLINK_PERIOD_MS;
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
        if (atomic_load(&s_mode) == TL_MODE_FLASHING_YELLOW) return true;
    }
    return false;
}

static bool blink_or_mode_change(bool red, bool yellow, bool green, uint32_t total_ms)
{
    uint32_t elapsed = 0;
    bool on = true;
    while (elapsed < total_ms) {
        set_lights(red && on, yellow && on, green && on);
        vTaskDelay(pdMS_TO_TICKS(TL_BLINK_PERIOD_MS));
        elapsed += TL_BLINK_PERIOD_MS;
        on = !on;
        if (atomic_load(&s_mode) == TL_MODE_FLASHING_YELLOW) return true;
    }
    return false;
}

void traffic_light_init(const traffic_light_pins_t *pins)
{
    s_pins = *pins;
    uint64_t mask = (1ULL << pins->red_pin) | (1ULL << pins->yellow_pin) | (1ULL << pins->green_pin);
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    set_lights(false, false, false);
}

traffic_light_state_t traffic_light_get_state(void) { return atomic_load(&s_state);  }

void traffic_light_toggle_mode(void)
{
    traffic_light_mode_t cur = atomic_load(&s_mode);
    atomic_store(&s_mode, cur == TL_MODE_NORMAL ? TL_MODE_FLASHING_YELLOW : TL_MODE_NORMAL);
}

void traffic_light_run_task(void *pvParameters)
{
    while (1) {
        if (atomic_load(&s_mode) == TL_MODE_FLASHING_YELLOW) {
            s_state = TL_STATE_FLASHING_YELLOW;
            set_lights(false, true, false);
            vTaskDelay(pdMS_TO_TICKS(TL_BLINK_PERIOD_MS));
            set_lights(false, false, false);
            vTaskDelay(pdMS_TO_TICKS(TL_BLINK_PERIOD_MS));
            continue;
        }

        s_state = TL_STATE_GREEN;
        ESP_LOGI(TAG, "GREEN");
        set_lights(false, false, true);
        if (wait_or_mode_change(TL_GREEN_DURATION_MS)) continue;

        s_state = TL_STATE_GREEN_FLASHING;
        ESP_LOGI(TAG, "GREEN (flashing)");
        if (blink_or_mode_change(false, false, true, TL_GREEN_FLASH_DURATION_MS)) continue;

        s_state = TL_STATE_YELLOW;
        ESP_LOGI(TAG, "YELLOW");
        set_lights(false, true, false);
        if (wait_or_mode_change(TL_YELLOW_DURATION_MS)) continue;

        s_state = TL_STATE_RED;
        ESP_LOGI(TAG, "RED");
        set_lights(true, false, false);
        if (wait_or_mode_change(TL_RED_DURATION_MS)) continue;

        s_state = TL_STATE_RED_YELLOW;
        ESP_LOGI(TAG, "RED + YELLOW");
        set_lights(true, true, false);
        if (wait_or_mode_change(TL_RED_YELLOW_DURATION_MS)) continue;
    }
}