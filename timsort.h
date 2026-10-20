#ifndef TIMSORT_H
#define TIMSORT_H

#include <stddef.h>

int timsort(void *base, size_t nel, size_t width,
            int (*compar)(const void *, const void *));

void timsort_merge_blocks(void *arr, size_t left, size_t mid, size_t right,
                          size_t width, int (*compar)(const void *, const void *));

#endif