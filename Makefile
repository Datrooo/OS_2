CC  := gcc
CFLAGS := -fsanitize=address -fsanitize=leak -Wall -Wextra -Werror -pthread
FLAGS_WITHOUT_SANITIZER := -Wall -Wextra -Werror -pthread

SRCS := $(shell find . -type f -name "*.c")

TARGETS := $(SRCS:.c=)

.PHONY: all nosan clean run run-nosan list clean-all

all: $(TARGETS)

nosan:
	@echo "Building all without sanitizers..."
	@$(MAKE) $(TARGETS) CFLAGS="$(FLAGS_WITHOUT_SANITIZER)"

%: %.c
	@echo "Compiling $< → $@"
	$(CC) $(CFLAGS) $< -o $@


run:
	@if [ -z "$(PATH)" ]; then \
		echo "Usage: make run PATH=task1/task1_4/task_c"; exit 2; \
	fi
	@ASAN_OPTIONS=detect_leaks=1 ./$(PATH)

run-nosan:
	@if [ -z "$(PATH)" ]; then \
		echo "Usage: make run-nosan PATH=task1/task1_4/task_c"; exit 2; \
	fi
	@./$(PATH)

list:
	@echo "Found sources:" && printf "  %s\n" $(SRCS)

clean:
	@find . -type f \( -perm -111 -a ! -name "*.c" \) -exec rm -f {} +

clean-all: clean
	@find . -name "*.o" -delete
