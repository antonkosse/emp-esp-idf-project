#include "line_view.h"
#include <stdio.h>
#include <string.h>

#define OUT_CAP (3 * (LINE_CAP - 1))

static void put_repeat(char ch, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        fputc(ch, stdout);
    }
}

static size_t common_prefix(const char *a, size_t a_len, const char *b, size_t b_len)
{
    size_t p = 0;
    while (p < a_len && p < b_len && a[p] == b[p])
    {
        p++;
    }

    return p;
}

void line_view_init(line_view_t *v)
{
    v->len = 0;
    v->shown[0] = '\0';
}

void line_view_show(line_view_t *v, const char *text)
{
    size_t new_len = strlen(text);
    if (new_len > LINE_CAP - 1)
    {
        new_len = LINE_CAP - 1;
    }
    size_t p = common_prefix(v->shown, v->len, text, new_len);
    put_repeat('\b', v->len - p);
    fwrite(text + p, 1, new_len - p, stdout);
    if (new_len < v->len)
    {
        size_t k = v->len - new_len; // how many old chars stick out
        put_repeat(' ', k);
        put_repeat('\b', k);
    }

    memcpy(v->shown, text, new_len);
    v->shown[new_len] = '\0';
    v->len = new_len;
    fflush(stdout);
}

void line_view_newline(line_view_t *v) {
    fputc('\n', stdout);
    fflush(stdout);
    v->len = 0;
    v->shown[0] = '\0';
}