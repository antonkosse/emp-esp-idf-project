// line_view.h
#pragma once
#include <stddef.h>

#define LINE_CAP 40   // max line length + 1 for '\0'

typedef struct {
    char   shown[LINE_CAP];   // text currently on screen
    size_t len;               // strlen(shown); cursor is right after it
} line_view_t;

void line_view_init(line_view_t *v);

// Redraw the current line to show `text`. `text` must not contain '\n'.
// Longer than LINE_CAP - 1 is truncated.
void line_view_show(line_view_t *v, const char *text);

// Keep the current line on screen and start a new empty one.
void line_view_newline(line_view_t *v);              // keep line, start new one