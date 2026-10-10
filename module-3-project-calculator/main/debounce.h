// debounce.h -- portable, no hardware and no clock access
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DEB_NONE,       // nothing changed this tick
    DEB_PRESS,      // stable released -> pressed
    DEB_RELEASE     // stable pressed -> released
} deb_event_t;

typedef struct {
    bool    stable;      // accepted state: true = pressed
    uint8_t count;       // ticks in a row raw != stable
    uint8_t threshold;   // ticks needed to accept a change
} debounce_t;

void        debounce_init(debounce_t *d, uint8_t threshold_ticks);
deb_event_t debounce_update(debounce_t *d, bool raw_pressed);   // call once per tick
bool        debounce_is_pressed(const debounce_t *d);