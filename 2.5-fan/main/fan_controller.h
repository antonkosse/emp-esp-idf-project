#pragma once
 
#include <stdbool.h>
 
#define DEBUG_TIMING 1
 
#if DEBUG_TIMING
    #define CYCLE_PERIOD_MS     (20ULL * 1000ULL)
    #define FAN_ON_DURATION_MS  (5ULL  * 1000ULL)
#else
    #define CYCLE_PERIOD_MS     (60ULL * 60ULL * 1000ULL)
    #define FAN_ON_DURATION_MS  (15ULL * 60ULL * 1000ULL)
#endif
 
// Configures GPIOs, then starts the fan ON/OFF cycle, the watchdog, and the
// dry-contact feedback heartbeat. Call once from app_main(). Nothing needs
// to be polled afterwards -- it all runs on its own via esp_timer callbacks.
void fan_controller_init(void);