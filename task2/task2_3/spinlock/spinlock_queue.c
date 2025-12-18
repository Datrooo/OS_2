#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "spinlock_queue.h"
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

    if (pthread_spin_init(&node->spinlock, PTHREAD_PROCESS_PRIVATE) != 0) {
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

        if (pthread_spin_destroy(&current->spinlock) != 0) {
            fprintf(stderr, "failed to destroy spinlock\n");
        }

        free(current);
        current = next;
    }

    free(storage);
}


void* find_rising_pairs(void* arg) {
    Storage* storage = (Storage*)arg;

    while (1) {
        int local_count = 0;

        if (pthread_spin_lock(&storage->first->spinlock) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_spin_unlock(&storage->first->spinlock);
            continue;
        }

        if (pthread_spin_lock(&current->spinlock) != 0) {
            pthread_spin_unlock(&storage->first->spinlock);
            continue;
        }

        pthread_spin_unlock(&storage->first->spinlock);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_spin_lock(&next_node->spinlock) != 0) {
                pthread_spin_unlock(&current->spinlock);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 < len2) local_count++;

            pthread_spin_unlock(&current->spinlock);
            current = next_node;
        }

        if (current) pthread_spin_unlock(&current->spinlock);

        atomic_fetch_add(&ascending_pairs, local_count);
        atomic_fetch_add(&iterations_count[0], 1);

        usleep(1000);
    }
    return NULL;
}

void* find_falling_pairs(void* arg) {
    Storage* storage = (Storage*)arg;

    while (1) {
        int local_count = 0;

        if (pthread_spin_lock(&storage->first->spinlock) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_spin_unlock(&storage->first->spinlock);
            continue;
        }

        if (pthread_spin_lock(&current->spinlock) != 0) {
            pthread_spin_unlock(&storage->first->spinlock);
            continue;
        }

        pthread_spin_unlock(&storage->first->spinlock);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_spin_lock(&next_node->spinlock) != 0) {
                pthread_spin_unlock(&current->spinlock);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 > len2) local_count++;

            pthread_spin_unlock(&current->spinlock);
            current = next_node;
        }

        if (current) pthread_spin_unlock(&current->spinlock);

        atomic_fetch_add(&descending_pairs, local_count);
        atomic_fetch_add(&iterations_count[1], 1);

        usleep(1000);
    }
    return NULL;
}

void* find_equal_pairs(void* arg) {
    Storage* storage = (Storage*)arg;

    while (1) {
        int local_count = 0;

        if (pthread_spin_lock(&storage->first->spinlock) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_spin_unlock(&storage->first->spinlock);
            continue;
        }

        if (pthread_spin_lock(&current->spinlock) != 0) {
            pthread_spin_unlock(&storage->first->spinlock);
            continue;
        }

        pthread_spin_unlock(&storage->first->spinlock);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_spin_lock(&next_node->spinlock) != 0) {
                pthread_spin_unlock(&current->spinlock);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 == len2) local_count++;

            pthread_spin_unlock(&current->spinlock);
            current = next_node;
        }

        if (current) pthread_spin_unlock(&current->spinlock);

        atomic_fetch_add(&equal_pairs, local_count);
        atomic_fetch_add(&iterations_count[2], 1);

        usleep(1000);
    }
    return NULL;
}

static int perform_swap(Node* prev, Node* curr, Node* next, int swap_index) {
    if (!prev || !curr || !next) return 0;

    if (pthread_spin_lock(&prev->spinlock) != 0) return 0;

    if (pthread_spin_lock(&curr->spinlock) != 0) {
        pthread_spin_unlock(&prev->spinlock);
        return 0;
    }

    if (pthread_spin_lock(&next->spinlock) != 0) {
        pthread_spin_unlock(&curr->spinlock);
        pthread_spin_unlock(&prev->spinlock);
        return 0;
    }

    if (prev->next != curr || curr->next != next) {
        pthread_spin_unlock(&next->spinlock);
        pthread_spin_unlock(&curr->spinlock);
        pthread_spin_unlock(&prev->spinlock);
        return 0;
    }

    curr->next = next->next;
    next->next = curr;
    prev->next = next;

    atomic_fetch_add(&swap_count[swap_index], 1);

    pthread_spin_unlock(&next->spinlock);
    pthread_spin_unlock(&curr->spinlock);
    pthread_spin_unlock(&prev->spinlock);

    return 1;
}

void* swap_thread_1(void* arg) {
    Storage* storage = (Storage*)arg;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)pthread_self();

    while (1) {
        Node* prev = storage->first;
        Node* curr = prev->next;
        int swapped = 0;

        while (curr && curr->next && !swapped) {
            Node* next = curr->next;

            if (should_swap(&seed)) {
                swapped = perform_swap(prev, curr, next, 0);
                if (swapped) break;
            }

            prev = curr;
            curr = next;
        }

        if (!swapped) usleep(2000);
        else usleep(10000);
    }
    return NULL;
}

void* swap_thread_2(void* arg) {
    Storage* storage = (Storage*)arg;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)pthread_self();

    while (1) {
        Node* prev = storage->first;
        Node* curr = prev->next;
        int swapped = 0;

        if (curr && curr->next) {
            prev = curr;
            curr = curr->next;
        }

        while (curr && curr->next && !swapped) {
            Node* next = curr->next;

            if (should_swap(&seed)) {
                swapped = perform_swap(prev, curr, next, 1);
                if (swapped) break;
            }

            prev = curr;
            curr = next;
        }

        if (!swapped) usleep(3000);
        else usleep(15000);
    }
    return NULL;
}

void* swap_thread_3(void* arg) {
    Storage* storage = (Storage*)arg;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)pthread_self();

    while (1) {
        Node* prev = storage->first;
        Node* curr = prev->next;
        int swapped = 0;

        while (curr && curr->next && !swapped) {
            Node* next = curr->next;

            if (should_swap(&seed)) {
                swapped = perform_swap(prev, curr, next, 2);
                if (swapped) break;
            }

            prev = curr;
            curr = next;
        }

        if (!swapped) usleep(4000);
        else usleep(20000);
    }
    return NULL;
}
