#include "traffic_light.h"
#include "pedestrian_traffic_light.h"
#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    traffic_light_pins_t pins = {
        .red_pin    = GPIO_NUM_4,
        .yellow_pin = GPIO_NUM_5,
        .green_pin  = GPIO_NUM_6,
    };
    traffic_light_init(&pins);

    pedestrian_light_pins_t pedestrian_pins = {
        .pedestrian_red_light   = GPIO_NUM_11,
        .pedestrian_green_light = GPIO_NUM_12,
    };
    pedestrian_traffic_light_init(&pedestrian_pins);

    xTaskCreate(traffic_light_run_task,             "traffic_light",            2048, NULL, 5, NULL);
    xTaskCreate(button_task,                        "button",                   2048, NULL, 5, NULL);
    xTaskCreate(pedestrian_traffic_light_run_task,  "pedestrian_traffic_light", 2048, NULL, 5, NULL);
}