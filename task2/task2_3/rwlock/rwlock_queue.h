#ifndef RWLOCK_QUEUE_H
#define RWLOCK_QUEUE_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

typedef struct _Node {
    char value[100];
    struct _Node* next;
    pthread_rwlock_t rwlock;
} Node;

typedef struct _Storage {
    Node* first;
    int size;
} Storage;

Node* create_node(const char* value);
Storage* create_storage(int size);
void free_storage(Storage* storage);
int get_length(const char* str);
int should_swap(unsigned* seed);

void* find_rising_pairs(void* arg);
void* find_falling_pairs(void* arg);
void* find_equal_pairs(void* arg);

void* swap_thread_1(void* arg);
void* swap_thread_2(void* arg);
void* swap_thread_3(void* arg);

#endif