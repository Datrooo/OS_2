CC = gcc
CFLAGS = -fsanitize=address -fsanitize=leak -Wall -Wextra -Werror -pthread
FLAGS_WITHOUT_SANITIZER = -Wall -Wextra -Werror -pthread
TARGETS = task_a task_b task_i task_i_without_sanitizer

all: $(TARGETS)

task_a: task_a.c
	$(CC) $(CFLAGS) $< -o $@

task_b: task_b.c
	$(CC) $(CFLAGS) $< -o $@

task_i: task_i.c
	$(CC) $(CFLAGS) $< -o $@

task_i_without_sanitizer: task_i.c
	$(CC) $(FLAGS_WITHOUT_SANITIZER) $< -o $@


run_a: task_a
	ASAN_OPTIONS=detect_leaks=1 ./task_a

run_b: task_b
	ASAN_OPTIONS=detect_leaks=1 ./task_b

run_i: task_i
	ASAN_OPTIONS=detect_leaks=1 ./task_i

run_i_without_sanitizer: task_i_without_sanitizer
	ASAN_OPTIONS=detect_leaks=1 ./task_i_without_sanitizer


clean:
	rm -f $(TARGETS)

.PHONY: all clean run_a run_b run_i run_i_without_sanitizer
