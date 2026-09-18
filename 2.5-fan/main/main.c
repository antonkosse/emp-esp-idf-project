#include "fan_controller.h"

void app_main(void) {
    fan_controller_init();
    // Nothing else to do here -- app_main() returns, and the esp_timer
    // service task keeps calling cycle_timer_cb() / heartbeat_cb() on
    // schedule from here on.
}