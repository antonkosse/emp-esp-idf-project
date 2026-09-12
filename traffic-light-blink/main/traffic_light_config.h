#pragma once

#define TL_GREEN_DURATION_MS         5000
#define TL_GREEN_FLASH_DURATION_MS   3000   // time spent flashing green before solid yellow
#define TL_YELLOW_DURATION_MS        3000
#define TL_RED_DURATION_MS           6000
#define TL_RED_YELLOW_DURATION_MS    2000

#define TL_BLINK_PERIOD_MS           500    // on/off half-period for any flashing state
#define TL_PEDESTRIAN_LIGHT_POLL_DURATION_MS 100
#define BUTTON_GPIO   GPIO_NUM_13   // adjust to your wiring
#define DEBOUNCE_MS   30