/*
 * Hardware (per schematic):
 *   GPIO20 --1k--> NPN Base   control line (HIGH = relay energized)
 *   GPIO8  <------ Relay NO   feedback; COM is on the shared ground rail,
 *                             so NO reads LOW only while actually closed
 *   Flyback diode is built into the relay module -- no firmware concern.
 */

#include "fan_controller.h"

#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "driver/gpio.h"

_Static_assert(FAN_ON_DURATION_MS < CYCLE_PERIOD_MS,
               "ON duration must be shorter than the full cycle period");

#define RELAY_CTRL_PIN   GPIO_NUM_20
#define FEEDBACK_PIN     GPIO_NUM_8
#define HEARTBEAT_MS     1000ULL
#define WDT_TIMEOUT_S    10

static const char *TAG = "fan_ctrl";

RTC_DATA_ATTR static bool rtc_fan_was_on = false;   // survives reset, fail-safe only

static volatile bool fan_is_on = false;
static esp_timer_handle_t cycle_timer;
static esp_timer_handle_t heartbeat_timer;


static void set_relay(bool on) {
    fan_is_on = on;
    gpio_set_level(RELAY_CTRL_PIN, on ? 1 : 0);
    rtc_fan_was_on = on;
}

static inline bool feedback_says_closed(void) {
    return gpio_get_level(FEEDBACK_PIN) == 0;   // NO shorted to grounded COM
}

static void schedule_next(uint64_t delay_ms) {
    esp_timer_start_once(cycle_timer, delay_ms * 1000ULL);
}


// Self-rescheduling: flips the relay once, then arms itself for whichever
// duration comes next. Runs in the esp_timer service task, independent of
// app_main() and of any task we create ourselves.
static void cycle_timer_cb(void *arg) {
    if (fan_is_on) {
        set_relay(false);
        ESP_LOGI(TAG, "Fan OFF");
        schedule_next(CYCLE_PERIOD_MS - FAN_ON_DURATION_MS);
    } else {
        set_relay(true);
        ESP_LOGI(TAG, "Fan ON");
        schedule_next(FAN_ON_DURATION_MS);
    }
}

// Periodic: feeds the watchdog and cross-checks the dry-contact feedback
// against what was commanded, so a stuck relay or dead coil gets logged.
static void heartbeat_cb(void *arg) {
    static bool subscribed = false;
    if (!subscribed) {
        esp_task_wdt_add(NULL);   // subscribes the currently-running task
        subscribed = true;
    }
    esp_task_wdt_reset(); // resets the time telling the watchdog that the program still breathes

    bool closed = feedback_says_closed();
    if (closed != fan_is_on) {
        ESP_LOGW(TAG, "Feedback mismatch: commanded=%s, contact=%s",
                 fan_is_on ? "ON" : "OFF", closed ? "CLOSED" : "OPEN");
    }
}


static void init_gpio(void) {
    gpio_config_t ctrl = {
        .pin_bit_mask = 1ULL << RELAY_CTRL_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE, // disable interupts
    };
    gpio_config(&ctrl);

    gpio_config_t fb = {
        .pin_bit_mask = 1ULL << FEEDBACK_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE, // disable interrupts per edge change
    };
    gpio_config(&fb);

    gpio_set_level(RELAY_CTRL_PIN, 0);   // fail-safe: OFF before anything else
    fan_is_on = false;
}

static void init_watchdog(void) {
    esp_task_wdt_config_t cfg = {
        .timeout_ms = WDT_TIMEOUT_S * 1000, // period for watchdog check
        .idle_core_mask = 0, // tells watchdog which tasks whould be watched for idle
        .trigger_panic = true, // actually reset the chip on panic
    };
    esp_task_wdt_reconfigure(&cfg);
}

static void init_timers(void) {
    const esp_timer_create_args_t cycle_args = {
        .callback = &cycle_timer_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fan_cycle",
    };
    esp_timer_create(&cycle_args, &cycle_timer);

    const esp_timer_create_args_t hb_args = {
        .callback = &heartbeat_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "heartbeat",
    };
    esp_timer_create(&hb_args, &heartbeat_timer);
    esp_timer_start_periodic(heartbeat_timer, HEARTBEAT_MS * 1000ULL);
}


void fan_controller_init(void) {
    init_gpio();

    if (rtc_fan_was_on) {
        ESP_LOGW(TAG, "Reboot detected while fan was ON -- forcing OFF");
    }
    rtc_fan_was_on = false;

    init_watchdog();
    init_timers();

    ESP_LOGI(TAG, "Fan controller ready, starting from OFF");
    schedule_next(CYCLE_PERIOD_MS - FAN_ON_DURATION_MS);
}