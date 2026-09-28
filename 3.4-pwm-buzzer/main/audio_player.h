// audio_player.h

#include <stdint.h> // uint32_t, uint16_t
#include <stddef.h> // size_t
#include <stdbool.h>

typedef struct
{
    uint32_t freq_hz;
    uint16_t ticks;
} note_t;

void player_start(const note_t *seq, size_t len);
void player_tick(void);
bool player_is_playing(void);