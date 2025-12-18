CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -pthread
AR      = ar
ARFLAGS = rcs

LIB     = libspinlock.a
TEST    = test

SRC     = spinlock.c
OBJ     = $(SRC:.c=.o)

TEST_SRC = test.c
TEST_OBJ = $(TEST_SRC:.c=.o)

all: $(LIB) $(TEST)

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(TEST): $(TEST_OBJ) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJ) -L. -lspinlock

%.o: %.c spinlock.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(LIB) $(TEST)

.PHONY: all clean
