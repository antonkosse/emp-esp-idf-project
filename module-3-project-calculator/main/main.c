#include <stdio.h>
#include "board_config.h"
#include "calc.h"
#include "debounce.h"
#include "enc_hal.h"
#include "line_view.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static calc_t calc;
static line_view_t view;
static debounce_t btn;
static char line[LINE_CAP];

static bool button_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg) == ESP_OK;
}

static bool button_is_pressed(void) {
    return gpio_get_level(BTN_PIN) == BTN_ACTIVE_LEVEL;
}

static void redraw(void) {
    calc_format(&calc, line, sizeof(line));
    line_view_show(&view, line);
}

void app_main(void)
{
    if (!enc_hal_init() || !button_init()) {
        printf("Hardware init failed\n");
        return;
    }

    calc_reset(&calc);
    line_view_init(&view);
    debounce_init(&btn, BTN_DEBOUNCE_MS / TICK_MS);

    printf("\n Calculator ready \n");
    redraw();

    TickType_t last_wake = xTaskGetTickCount();

    for(;;) {
        int32_t steps = enc_hal_read_steps();
        if (steps != 0) {
            calc_on_rotate(&calc, steps);
            redraw();
        }
        if (debounce_update(&btn, button_is_pressed()) == DEB_PRESS) {
            bool was_done = (calc.state == CALC_DONE);
            calc_on_press(&calc);
            if (was_done) {
                line_view_newline(&view);
            }
            redraw();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TICK_MS));
    }
}
