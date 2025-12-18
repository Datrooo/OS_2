CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wpedantic -pthread
LDFLAGS := -pthread

TARGET := mutex

SRCS := mutex.c mutex_queue.c counters.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) 1000

clean:
	rm -f $(OBJS) $(TARGET)
