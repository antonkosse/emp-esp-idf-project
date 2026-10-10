// calc.h
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum { CALC_ENTER_A, CALC_ENTER_OP, CALC_ENTER_B, CALC_DONE } calc_state_t;
typedef enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_COUNT } calc_op_t;

typedef struct {
    calc_state_t state;
    int32_t      a, b;
    calc_op_t    op;
} calc_t;

void   calc_reset(calc_t *c);
void   calc_on_rotate(calc_t *c, int32_t steps);
void   calc_on_press(calc_t *c);
size_t calc_format(const calc_t *c, char *buf, size_t cap);   // whole line as text