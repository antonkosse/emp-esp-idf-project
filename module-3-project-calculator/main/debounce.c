#include "debounce.h"
#include <stdbool.h>
#include <stdint.h>

void debounce_init(debounce_t *d, uint8_t threshold_ticks) {
    d->stable = false;
    d->count = 0;
    d->threshold = threshold_ticks;
}

deb_event_t debounce_update(debounce_t *d, bool raw_pressed) {
    if (d->stable == raw_pressed) {
        d->count = 0;
        return DEB_NONE;
    }
    d->count++;
    if (d->count < d->threshold) {
        return DEB_NONE;
    }
    d->stable = raw_pressed;
    d->count  = 0;

    return raw_pressed ? DEB_PRESS : DEB_RELEASE;
}

bool debounce_is_pressed(const debounce_t *d) {
    return d->stable;
}