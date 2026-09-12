#pragma once
#include <stdbool.h>
#include "driver/gpio.h"

typedef enum {
    TL_STATE_GREEN = 0,
    TL_STATE_GREEN_FLASHING,
    TL_STATE_YELLOW,
    TL_STATE_RED,
    TL_STATE_RED_YELLOW,
    TL_STATE_FLASHING_YELLOW,
} traffic_light_state_t;

typedef enum {
    TL_MODE_NORMAL = 0,
    TL_MODE_FLASHING_YELLOW,
} traffic_light_mode_t;

typedef struct {
    gpio_num_t red_pin;
    gpio_num_t yellow_pin;
    gpio_num_t green_pin;
} traffic_light_pins_t;

void traffic_light_init(const traffic_light_pins_t *pins);
void traffic_light_run_task(void *pvParameters);
void traffic_light_toggle_mode(void);
traffic_light_state_t traffic_light_get_state(void);