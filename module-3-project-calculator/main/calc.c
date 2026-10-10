#include "calc.h"
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#define CALC_MIN (-999)
#define CALC_MAX 999
#define FMT_A "%" PRId32
#define FMT_OP "%" PRId32 " %c "
#define FMT_B_POS "%" PRId32 " %c %" PRId32
#define FMT_B_NEG "%" PRId32 " %c (%" PRId32 ")"
#define FMT_DONE_POS "%" PRId32 " %c %" PRId32 " = %s"
#define FMT_DONE_NEG "%" PRId32 " %c (%" PRId32 ") = %s"

static const char OP_CHARS[OP_COUNT] = {'+', '-', '*', '/'};
_Static_assert(sizeof(OP_CHARS) == OP_COUNT, "OP_CHARS must match calc_op_t");

static int32_t clamp(int32_t value, int32_t low_value, int32_t  high_value)
{
    if (value > high_value)
    {
        return high_value;
    }
    if (value < low_value)
    {
        return low_value;
    }
    return value;
}

static calc_op_t wrap_op(calc_op_t op, int32_t steps)
{
    int32_t i = (int32_t)op + steps;
    return (calc_op_t)((i % OP_COUNT + OP_COUNT) % OP_COUNT);
}

static void write_result(const calc_t *c, char *out, size_t cap)
{
    switch (c->op)
    {
    case OP_ADD:
        snprintf(out, cap, "%" PRId32, c->a + c->b);
        break;
    case OP_SUB:
        snprintf(out, cap, "%" PRId32, c->a - c->b);
        break;
    case OP_MUL:
        snprintf(out, cap, "%" PRId32, c->a * c->b);
        break;
    case OP_DIV:
    {
        if (c->b == 0)
        {
            snprintf(out, cap, "ERR");
            break;
        }
        bool neg = (c->a < 0) != (c->b < 0);
        int32_t q100 = (int32_t)(((abs(c->a) * 100 + abs(c->b) / 2)) / abs(c->b));
        const char *sign = (neg && q100 != 0) ? "-" : "";
        snprintf(out, cap, "%s%" PRId32 ".%02" PRId32, sign, q100 / 100, q100 % 100);
        break;
    }

    default:
        out[0] = '\0';
        break;
    }
}

void calc_reset(calc_t *c)
{
    c->op = OP_ADD;
    c->state = CALC_ENTER_A;
    c->a = 0;
    c->b = 0;
}

void calc_on_rotate(calc_t *c, int32_t steps) {
    switch(c->state) {
        case CALC_ENTER_A:
            c->a = clamp(c->a + steps, CALC_MIN, CALC_MAX);
            break;
        case CALC_ENTER_OP:
            c->op = wrap_op(c->op, steps);
            break;
        case CALC_ENTER_B:
            c->b = clamp(c->b + steps, CALC_MIN, CALC_MAX);
            break;
        default:
            break;
    }
}

void calc_on_press(calc_t *c) {
    switch (c->state)
    {
        case CALC_ENTER_A:
            c->state = CALC_ENTER_OP;
            break;
        case CALC_ENTER_OP:
            c->state = CALC_ENTER_B;
            break;
        case CALC_ENTER_B:
            c->state = CALC_DONE;
            break; 
        case CALC_DONE:
            calc_reset(c);
            break;
    default:
        break;
    }
}

size_t calc_format(const calc_t *c, char *buf, size_t cap)
{
    if (cap == 0)
    {
        return 0;
    }
    buf[0] = '\0';
    char ch = OP_CHARS[c->op];
    int n = 0;
    switch (c->state)
    {
    case CALC_ENTER_A:
        n = snprintf(buf, cap, FMT_A, c->a);
        break;
    case CALC_ENTER_OP:
        n = snprintf(buf, cap, FMT_OP, c->a, ch);
        break;
    case CALC_ENTER_B:
        n = snprintf(buf, cap, c->b < 0 ? FMT_B_NEG : FMT_B_POS, c->a, ch, c->b);
        break;
    case CALC_DONE:
    {
        char res[16];
        write_result(c, res, sizeof(res));
        n = snprintf(buf, cap, c->b < 0 ? FMT_DONE_NEG : FMT_DONE_POS, c->a, ch, c->b, res);
        break;
    }

    default:
        break;
    }
    if (n < 0)
    {
        n = 0;
    }

    if ((size_t)n >= cap)
    {
        n = cap - 1;
    }

    return (size_t)n;
}
