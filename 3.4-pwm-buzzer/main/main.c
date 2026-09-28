#include <stdio.h>
#include "esp_log.h"
#include "audio_player.h"
#include "buzzer_hal.h"
#include "esp_timer.h"

#define TICK_MS 50
#define MS_TO_TICKS(ms) (((ms) + TICK_MS / 2) / TICK_MS)
#define COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))

static const note_t melody[] = {
    {330, MS_TO_TICKS(300)}, // E4
    {330, MS_TO_TICKS(300)}, // E4
    {349, MS_TO_TICKS(300)}, // F4
    {392, MS_TO_TICKS(300)}, // G4
    {392, MS_TO_TICKS(300)}, // G4
    {349, MS_TO_TICKS(300)}, // F4
    {330, MS_TO_TICKS(300)}, // E4
    {294, MS_TO_TICKS(300)}, // D4
    {262, MS_TO_TICKS(300)}, // C4
    {262, MS_TO_TICKS(300)}, // C4
    {294, MS_TO_TICKS(300)}, // D4
    {330, MS_TO_TICKS(300)}, // E4
    {330, MS_TO_TICKS(450)}, // E4 (dotted quarter)
    {294, MS_TO_TICKS(150)}, // D4 (eighth)
    {294, MS_TO_TICKS(600)}, // D4 (half note)
    {0, MS_TO_TICKS(300)},   // rest before loop
};

static void setup(void)
{
    buzzer_hal_init();
    player_start(melody, COUNT_OF(melody));
}
void app_main(void)
{
    setup();
    int64_t next = esp_timer_get_time() + TICK_MS * 1000;
    for (;;)
    {
        int64_t now = esp_timer_get_time();
        if (now - next >= 0)
        {
            next += TICK_MS * 1000;
            player_tick();
        }
        if (!player_is_playing())
        {
            player_start(melody, COUNT_OF(melody));
        }
    }
}