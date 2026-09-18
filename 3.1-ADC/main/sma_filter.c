#include "sma_filter.h"


int calculate_average(const int *samples, size_t count)
{
    int sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += samples[i];
    }
    return sum / (int)count;
}

void sma_filter_init(sma_filter_t *filter) {
    filter->write_index = 0;
    filter->count_filled = 0;

}
int sma_filter_update(sma_filter_t *filter, int new_sample)
{
    filter->samples[filter->write_index] = new_sample;
    filter->write_index = (filter->write_index + 1) % SMA_WINDOW_SIZE;
    if (filter->count_filled < SMA_WINDOW_SIZE) {
        filter->count_filled++;
    }
    return calculate_average(filter->samples, filter->count_filled);
}