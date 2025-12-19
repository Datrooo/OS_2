#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "mutex_queue.h"
#include "counters.h"


int get_length(const char* str) {
    return (int)strlen(str);
}

int should_swap(unsigned* seed) {
    return (rand_r(seed) % 3) == 0;
}

Node* create_node(const char* value) {
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) return NULL;

    if (value) {
        strncpy(node->value, value, sizeof(node->value) - 1);
        node->value[sizeof(node->value) - 1] = '\0';
    } else {
        node->value[0] = '\0';
    }

    node->next = NULL;

    if (pthread_mutex_init(&node->mutex, NULL) != 0) {
        free(node);
        return NULL;
    }
    return node;
}


Storage* create_storage(int size) {
    Storage* storage = (Storage*)malloc(sizeof(Storage));
    if (!storage) return NULL;

    storage->size = size;

    storage->first = create_node(NULL);
    if (!storage->first) {
        free(storage);
        return NULL;
    }

    Node* current = storage->first;

    for (int i = 0; i < size; i++) {
        char value[100];
        int length = rand() % 50 + 10;
        for (int j = 0; j < length; j++) {
            value[j] = (char)('a' + rand() % 26);
        }
        value[length] = '\0';

        Node* new_node = create_node(value);
        if (!new_node) {
            free_storage(storage);
            return NULL;
        }

        current->next = new_node;
        current = new_node;
    }

    return storage;
}

void free_storage(Storage* storage) {
    if (!storage) return;

    Node* current = storage->first;
    while (current) {
        Node* next = current->next;
        if (pthread_mutex_destroy(&current->mutex) != 0) {
            fprintf(stderr, "failed to destroy mutex\n");
        }

        free(current);
        current = next;
    }

    free(storage);
}

void* find_rising_pairs(void* arg) {
    Storage* storage = (Storage*)arg;

    while (1) {
        pthread_testcancel();

        int local_count = 0;

        if (pthread_mutex_lock(&storage->first->mutex) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_mutex_unlock(&storage->first->mutex);
            continue;
        }

        if (pthread_mutex_lock(&current->mutex) != 0) {
            pthread_mutex_unlock(&storage->first->mutex);
            continue;
        }

        pthread_mutex_unlock(&storage->first->mutex);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_mutex_lock(&next_node->mutex) != 0) {
                pthread_mutex_unlock(&current->mutex);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 < len2) local_count++;

            pthread_mutex_unlock(&current->mutex);
            

            current = next_node;
        }

        if (current) pthread_mutex_unlock(&current->mutex);

        atomic_fetch_add(&ascending_pairs, local_count);
        atomic_fetch_add(&iterations_count[0], 1);
    }
    return NULL;
}

void* find_falling_pairs(void* arg) {
    Storage* storage = (Storage*)arg;

    while (1) {
        pthread_testcancel();

        int local_count = 0;

        if (pthread_mutex_lock(&storage->first->mutex) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_mutex_unlock(&storage->first->mutex);
            continue;
        }

        if (pthread_mutex_lock(&current->mutex) != 0) {
            pthread_mutex_unlock(&storage->first->mutex);
            continue;
        }

        pthread_mutex_unlock(&storage->first->mutex);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_mutex_lock(&next_node->mutex) != 0) {
                pthread_mutex_unlock(&current->mutex);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 > len2) local_count++;

            pthread_mutex_unlock(&current->mutex);
            current = next_node;
        }

        if (current) pthread_mutex_unlock(&current->mutex);

        atomic_fetch_add(&descending_pairs, local_count);
        atomic_fetch_add(&iterations_count[1], 1);
    }
    return NULL;
}

void* find_equal_pairs(void* arg) {
    Storage* storage = (Storage*)arg;

    while (1) {
        pthread_testcancel();

        int local_count = 0;

        if (pthread_mutex_lock(&storage->first->mutex) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_mutex_unlock(&storage->first->mutex);
            continue;
        }

        if (pthread_mutex_lock(&current->mutex) != 0) {
            pthread_mutex_unlock(&storage->first->mutex);
            continue;
        }

        pthread_mutex_unlock(&storage->first->mutex);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_mutex_lock(&next_node->mutex) != 0) {
                pthread_mutex_unlock(&current->mutex);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 == len2) local_count++;

            pthread_mutex_unlock(&current->mutex);
            current = next_node;
        }

        if (current) pthread_mutex_unlock(&current->mutex);

        atomic_fetch_add(&equal_pairs, local_count);
        atomic_fetch_add(&iterations_count[2], 1);
    }
    return NULL;
}

int perform_swap_locked(Node* prev, Node* curr, Node* next, int swap_index) {
    if (prev->next != curr || curr->next != next) return 0;

    curr->next = next->next;
    next->next = curr;
    prev->next = next;

    atomic_fetch_add(&swap_count[swap_index], 1);
    return 1;
}

void* swap_thread_common(void* arg, int swap_index, int start_skip_pairs) {
    Storage* storage = (Storage*)arg;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)pthread_self();

    while (1) {
        pthread_testcancel();

        int did_swap = 0;

        Node* prev = storage->first;
        if (pthread_mutex_lock(&prev->mutex) != 0) {
            continue;
        }

        Node* curr = prev->next;
        if (!curr) {
            pthread_mutex_unlock(&prev->mutex);
            continue;
        }

        if (pthread_mutex_lock(&curr->mutex) != 0) {
            pthread_mutex_unlock(&prev->mutex);
            continue;
        }

        for (int k = 0; k < start_skip_pairs; k++) {
            Node* next = curr->next;
            if (!next) break;

            if (pthread_mutex_lock(&next->mutex) != 0) {
                break;
            }

            pthread_mutex_unlock(&prev->mutex);
            prev = curr;
            curr = next;
        }

        while (curr && curr->next) {
            Node* next = curr->next;

            if (pthread_mutex_lock(&next->mutex) != 0) {
                break;
            }

            if (should_swap(&seed)) {
                (void)perform_swap_locked(prev, curr, next, swap_index);

                pthread_mutex_unlock(&next->mutex);
                pthread_mutex_unlock(&curr->mutex);
                pthread_mutex_unlock(&prev->mutex);

                did_swap = 1;
                break;
            }

            pthread_mutex_unlock(&prev->mutex);
            prev = curr;
            curr = next;
        }

        if (!did_swap) {
            pthread_mutex_unlock(&curr->mutex);
            pthread_mutex_unlock(&prev->mutex);
        }
    }

    return NULL;
}

void* swap_thread_1(void* arg) { return swap_thread_common(arg, 0, 0); }
void* swap_thread_2(void* arg) { return swap_thread_common(arg, 1, 5); }
void* swap_thread_3(void* arg) { return swap_thread_common(arg, 2, 10); }
