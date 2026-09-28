#include <stdint.h>   // uint32_t, uint16_t
#include <stddef.h>   // size_t
#include <stdbool.h>
#include "audio_player.h"
#include "buzzer_hal.h"

static const note_t *seq;
static size_t len, idx;
static uint16_t ticks_left; 
static bool playing;

void player_tick(void) {
    if (!playing) return;
    if (--ticks_left > 0) return; 
    if (++idx >= len) { 
        playing = false;
        buzzer_hal_set_tone(0);
        return; 
    }
    buzzer_hal_set_tone(seq[idx].freq_hz);
    ticks_left = seq[idx].ticks;
}

void player_start(const note_t *notes, size_t count)
{
    seq = notes;
    len = count;
    idx = 0;
    if (len == 0) {
        playing = false;
        return;
    }   // guard: empty melody

    buzzer_hal_set_tone(seq[0].freq_hz);
    ticks_left = seq[0].ticks;
    playing = true;
}

bool player_is_playing(void)
{
    return playing;
}