#pragma once
#include <stdbool.h>
#include "driver/gpio.h"

typedef struct {
    gpio_num_t pedestrian_red_light;
    gpio_num_t pedestrian_green_light;
} pedestrian_light_pins_t;

void pedestrian_traffic_light_init(const pedestrian_light_pins_t *pins);
void pedestrian_traffic_light_run_task(void *pvParameters);