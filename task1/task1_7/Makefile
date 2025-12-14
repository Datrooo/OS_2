CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -fsanitize=address -fsanitize=leak -pthread -Isrc

SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests

LIB_UTHREAD = $(BUILD_DIR)/libuthread.a
LIB_UTHREAD_MAP = $(BUILD_DIR)/libuthread_map.a

UTHREAD_SRC = $(SRC_DIR)/uthread.c
UTHREAD_MAP_SRC = $(SRC_DIR)/uthread_map.c

TEST_UTHREAD = $(BUILD_DIR)/test_uthread
TEST = $(BUILD_DIR)/test
TEST_REUSE = $(BUILD_DIR)/test_reuse
TEST_UTHREAD_ERRORS = $(BUILD_DIR)/test_uthread_errors
TEST_UTHREAD_MAP_ERRORS = $(BUILD_DIR)/test_uthread_map_errors

all: $(LIB_UTHREAD) $(LIB_UTHREAD_MAP) $(TEST_UTHREAD) $(TEST) $(TEST_REUSE) $(TEST_UTHREAD_ERRORS) $(TEST_UTHREAD_MAP_ERRORS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/uthread.o: $(UTHREAD_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uthread_map.o: $(UTHREAD_MAP_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_UTHREAD): $(BUILD_DIR)/uthread.o
	ar rcs $@ $^

$(LIB_UTHREAD_MAP): $(BUILD_DIR)/uthread_map.o
	ar rcs $@ $^

$(TEST_UTHREAD): $(TEST_DIR)/test_uthread.c $(LIB_UTHREAD)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -luthread


$(TEST): $(TEST_DIR)/test.c $(LIB_UTHREAD) $(LIB_UTHREAD_MAP)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -luthread -luthread_map

$(TEST_REUSE): $(TEST_DIR)/test_reuse.c $(LIB_UTHREAD) $(LIB_UTHREAD_MAP)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -luthread

$(TEST_UTHREAD_ERRORS): $(TEST_DIR)/test_uthread_errors.c $(LIB_UTHREAD)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -luthread

$(TEST_UTHREAD_MAP_ERRORS): $(TEST_DIR)/test_uthread_map_errors.c $(LIB_UTHREAD) $(LIB_UTHREAD_MAP)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -luthread -luthread_map

test:
	@echo "Running test_uthread"
	$(TEST_UTHREAD)
	@echo "#############################################################"
	@echo "Running test_uthread_errors"
	$(TEST_UTHREAD_ERRORS)
	@echo "#############################################################"
	@echo "Running test_reuse"
	$(TEST_REUSE)
	@echo "#############################################################"
	@echo "Running test mapping"
	$(TEST)
	@echo "#############################################################"
	@echo "Running test_uthread_map_errors"
	$(TEST_UTHREAD_MAP_ERRORS)

clean:
	rm -rf $(BUILD_DIR)
