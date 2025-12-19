#define _GNU_SOURCE
#include <stdio.h>
#include <stdatomic.h>
#include "counters.h"

atomic_int ascending_pairs = 0;
atomic_int descending_pairs = 0;
atomic_int equal_pairs = 0;
atomic_int iterations_count[3] = {0, 0, 0};
atomic_int swap_count[3] = {0, 0, 0};

void print_stats(void) {
    int asc, desc, eq, iter[3], swaps[3];
    read_atomic_stats(&asc, &desc, &eq, iter, swaps);
    printf("################\n");
    printf("pair stats:\n");
    printf("\trising pairs: %d\n", asc);
    printf("\tfalling pairs: %d\n", desc);
    printf("\tequal pairs: %d\n", eq);
    printf("\titerations: [%d %d %d]\n", iter[0], iter[1], iter[2]);

    printf("\tswaps: %d %d %d\n", swaps[0], swaps[1], swaps[2]);
    printf("#################\n");
}

void read_atomic_stats(int* asc, int* desc, int* eq, int* iter, int* swaps) {
    *asc = atomic_load(&ascending_pairs);
    *desc = atomic_load(&descending_pairs);
    *eq = atomic_load(&equal_pairs);

    for (int i = 0; i < 3; i++) {
        iter[i] = atomic_load(&iterations_count[i]);
        swaps[i] = atomic_load(&swap_count[i]);
    }
}

void reset_atomic_counters() {
    atomic_store(&ascending_pairs, 0);
    atomic_store(&descending_pairs, 0);
    atomic_store(&equal_pairs, 0);

    for (int i = 0; i < 3; i++) {
        atomic_store(&iterations_count[i], 0);
        atomic_store(&swap_count[i], 0);
    }
}