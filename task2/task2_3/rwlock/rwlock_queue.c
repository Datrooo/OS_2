#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "rwlock_queue.h"
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
        node->value[0] = '\0'; // sentinel
    }

    node->next = NULL;

    // rwlock init: attr=NULL -> default attrs [web:212]
    if (pthread_rwlock_init(&node->rwlock, NULL) != 0) {
        free(node);
        return NULL;
    }

    return node;
}

Storage* create_storage(int size) {
    Storage* storage = (Storage*)malloc(sizeof(Storage));
    if (!storage) return NULL;

    storage->size = size;

    storage->first = create_node(NULL); // sentinel-head
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

        // destroy запрещён, если lock удерживается (иначе UB) [web:212]
        if (pthread_rwlock_destroy(&current->rwlock) != 0) {
            fprintf(stderr, "failed to destroy rwlock\n");
        }

        free(current);
        current = next;
    }

    free(storage);
}

// ---------------- readers ----------------
// Важно: читающие потоки берут rdlock на узлы [web:21]

void* find_rising_pairs(void* arg) {
    Storage* storage = (Storage*)arg;

    while (1) {
        int local_count = 0;

        // чтобы безопасно прочитать first->next, берём rdlock на sentinel
        if (pthread_rwlock_rdlock(&storage->first->rwlock) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_rwlock_unlock(&storage->first->rwlock);
            continue;
        }

        if (pthread_rwlock_rdlock(&current->rwlock) != 0) {
            pthread_rwlock_unlock(&storage->first->rwlock);
            continue;
        }

        pthread_rwlock_unlock(&storage->first->rwlock);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_rwlock_rdlock(&next_node->rwlock) != 0) {
                pthread_rwlock_unlock(&current->rwlock);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 < len2) local_count++;

            pthread_rwlock_unlock(&current->rwlock);
            current = next_node; // next_node остаётся залоченным на чтение
        }

        if (current) pthread_rwlock_unlock(&current->rwlock);

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

        if (pthread_rwlock_rdlock(&storage->first->rwlock) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_rwlock_unlock(&storage->first->rwlock);
            continue;
        }

        if (pthread_rwlock_rdlock(&current->rwlock) != 0) {
            pthread_rwlock_unlock(&storage->first->rwlock);
            continue;
        }

        pthread_rwlock_unlock(&storage->first->rwlock);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_rwlock_rdlock(&next_node->rwlock) != 0) {
                pthread_rwlock_unlock(&current->rwlock);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 > len2) local_count++;

            pthread_rwlock_unlock(&current->rwlock);
            current = next_node;
        }

        if (current) pthread_rwlock_unlock(&current->rwlock);

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

        if (pthread_rwlock_rdlock(&storage->first->rwlock) != 0) continue;

        Node* current = storage->first->next;
        if (!current) {
            pthread_rwlock_unlock(&storage->first->rwlock);
            continue;
        }

        if (pthread_rwlock_rdlock(&current->rwlock) != 0) {
            pthread_rwlock_unlock(&storage->first->rwlock);
            continue;
        }

        pthread_rwlock_unlock(&storage->first->rwlock);

        while (current && current->next) {
            Node* next_node = current->next;

            if (pthread_rwlock_rdlock(&next_node->rwlock) != 0) {
                pthread_rwlock_unlock(&current->rwlock);
                current = NULL;
                break;
            }

            int len1 = get_length(current->value);
            int len2 = get_length(next_node->value);
            if (len1 == len2) local_count++;

            pthread_rwlock_unlock(&current->rwlock);
            current = next_node;
        }

        if (current) pthread_rwlock_unlock(&current->rwlock);

        atomic_fetch_add(&equal_pairs, local_count);
        atomic_fetch_add(&iterations_count[2], 1);

        usleep(1000);
    }
    return NULL;
}

// ---------------- writers (swap) ----------------
// Писатели берут wrlock на prev/curr/next, т.к. меняют ссылки [web:21]

static int perform_swap(Node* prev, Node* curr, Node* next, int swap_index) {
    if (!prev || !curr || !next) return 0;

    if (pthread_rwlock_wrlock(&prev->rwlock) != 0) return 0;

    if (pthread_rwlock_wrlock(&curr->rwlock) != 0) {
        pthread_rwlock_unlock(&prev->rwlock);
        return 0;
    }

    if (pthread_rwlock_wrlock(&next->rwlock) != 0) {
        pthread_rwlock_unlock(&curr->rwlock);
        pthread_rwlock_unlock(&prev->rwlock);
        return 0;
    }

    if (prev->next != curr || curr->next != next) {
        pthread_rwlock_unlock(&next->rwlock);
        pthread_rwlock_unlock(&curr->rwlock);
        pthread_rwlock_unlock(&prev->rwlock);
        return 0;
    }

    curr->next = next->next;
    next->next = curr;
    prev->next = next;

    atomic_fetch_add(&swap_count[swap_index], 1);

    pthread_rwlock_unlock(&next->rwlock);
    pthread_rwlock_unlock(&curr->rwlock);
    pthread_rwlock_unlock(&prev->rwlock);

    return 1;
}

void* swap_thread_1(void* arg) {
    Storage* storage = (Storage*)arg;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)pthread_self();

    while (1) {
        Node* prev = storage->first;   // sentinel
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
