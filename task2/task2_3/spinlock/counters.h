#ifndef __FITOS_COUNTERS_H__
#define __FITOS_COUNTERS_H__

#define _GNU_SOURCE
#include <stdio.h>
#include <stdatomic.h>

extern atomic_int ascending_pairs;
extern atomic_int descending_pairs;
extern atomic_int equal_pairs;
extern atomic_int iterations_count[3];
extern atomic_int swap_count[3];

void read_atomic_stats(int* asc, int* desc, int* eq, int* iter, int* swaps);
void print_stats(void);  
void reset_atomic_counters(void);

#endif