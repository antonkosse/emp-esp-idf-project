#include <stdio.h>
#include "config.h"
#include "i2c_hal.h"
#include "datetime.h"
#include "ds1307.h"
#include "oled.h"
#include "clock_view.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define FAIL_LIMIT 3

_Static_assert(pdMS_TO_TICKS(POLL_MS) > 0);

static const char* TAG="4.3-oled-rtc";

static const char* status_text(ds1307_status_t status) {
    switch (status) {
        case DS1307_HALTED: return "SET TIME";
        case DS1307_BAD_DATA: return "RTC DATA ERR";
        default: return "RTC ERROR";
    }
}

static bool display_init(void) {
    uint8_t candidates[] = {ADDR_OLED, ADDR_OLED_ALT};
    uint8_t count = sizeof(candidates) / sizeof(candidates[0]);

    for (int i = 0; i < count; i++) {
        uint8_t addr = candidates[i];
        if (i2c_hal_probe(addr) == I2C_OK) {
            if (oled_init(addr, OLED_HEIGHT)) {
                ESP_LOGI(TAG, "OLED at 0x%02x", addr);
                return true;
            }
            ESP_LOGE(TAG, "OLED init failed at 0x%02x", addr);
            return false;
        }
    }

    ESP_LOGW(TAG, "no OLED at 0x%02x/0x%02x", ADDR_OLED, ADDR_OLED_ALT);

    return false;
}

static ds1307_status_t rtc_init(void) {
    datetime_t t;
    ds1307_status_t status = ds1307_init();
    if (status == DS1307_BUS_ERR) {
        ESP_LOGE(TAG, "no DS1307 at 0x68");
        return status;
    }

    if (RTC_SET_FROM_BUILD || status == DS1307_HALTED) {
        if(!datetime_from_build(__DATE__, __TIME__, &t)) {
            ESP_LOGE(TAG, "Cannot parse build time");
            return DS1307_BAD_DATA;
        }

        status = ds1307_write(&t);
        ESP_LOGI(TAG, "Time set from build stamp");
    }

    return status;
}

void app_main(void)
{
     datetime_t now;
    if (!i2c_hal_init()) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return;
    }

    bool display_ok = display_init();
    ds1307_status_t st = rtc_init();

    if (display_ok && st != DS1307_OK) {
        clock_view_message(status_text(st));
        oled_flush();
    }

    uint8_t last_sec = 0xFF;
    uint8_t fails = 0;
    TickType_t last_wake = xTaskGetTickCount();

    for(;;) {
        st = ds1307_read(&now);
        if (st == DS1307_OK) {
            if (fails >= FAIL_LIMIT) {
                ESP_LOGI(TAG, "RTC back");
            }
            fails = 0;

            if (now.sec != last_sec) {
                last_sec = now.sec;
                if(display_ok) {
                    clock_view_draw(&now);
                    if (!oled_flush()) {
                        ESP_LOGW(TAG, "OLED flush failed");
                    }
                }
            }
        } else {
            if (fails < FAIL_LIMIT) {
                fails++;
                if (fails == FAIL_LIMIT) {
                ESP_LOGW(TAG, "RTC: %s", status_text(st));
                if (display_ok) {
                    clock_view_message(status_text(st));
                    oled_flush();
                }
            }
            }

            last_sec = 0xFF;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(POLL_MS));
    }


}