#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Define GPIO pins for ESP32-S3 (adjust pin numbers to match your setup)
#define LED1_PIN GPIO_NUM_4
#define LED2_PIN GPIO_NUM_5
#define LED3_PIN GPIO_NUM_6

typedef struct {
    gpio_num_t pin;
    uint32_t interval_ms;
    uint64_t last_toggle_ms;
    uint32_t state;
} LedTask;

// Equivalent to Arduino's millis()
static uint64_t millis(void) {
    return esp_timer_get_time() / 1000ULL;
}

void app_main(void) {
    LedTask leds[] = {
        { .pin = LED1_PIN, .interval_ms = 200,  .last_toggle_ms = 0, .state = 0 },
        { .pin = LED2_PIN, .interval_ms = 500,  .last_toggle_ms = 0, .state = 0 },
        { .pin = LED3_PIN, .interval_ms = 1000, .last_toggle_ms = 0, .state = 0 }
    };

    // Configure GPIO pins as outputs
    for (int i = 0; i < 3; i++) {
        gpio_reset_pin(leds[i].pin);
        gpio_set_direction(leds[i].pin, GPIO_MODE_OUTPUT);
    }

    while (1) {
        uint64_t current_ms = millis();

        // Non-blocking state evaluation
        for (int i = 0; i < 3; i++) {
            if (current_ms - leds[i].last_toggle_ms >= leds[i].interval_ms) {
                leds[i].last_toggle_ms = current_ms;
                leds[i].state = !leds[i].state;
                gpio_set_level(leds[i].pin, leds[i].state);
            }
        }

        // A 1ms FreeRTOS yield keeps FreeRTOS task watchdog happy without 
        // blocking or altering your timing logic.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}