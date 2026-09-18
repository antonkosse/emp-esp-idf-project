#pragma once

#include <stdio.h>

#define SMA_WINDOW_SIZE 10

typedef struct {
    int    samples[SMA_WINDOW_SIZE];
    size_t write_index;
    size_t count_filled;
} sma_filter_t;

void sma_filter_init(sma_filter_t *filter);
int  sma_filter_update(sma_filter_t *filter, int new_sample);