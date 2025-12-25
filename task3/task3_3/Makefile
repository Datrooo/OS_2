CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -Iinclude
LDFLAGS = -lev -lpthread

SOURCES = src/main.c src/loop.c src/session.c src/cache.c src/key_builder.c \
          src/net.c src/parse.c src/downloader.c src/gc.c src/dirty.c src/stream.c src/subscribers.c

OBJECTS = $(SOURCES:.c=.o)
EXECUTABLE = proxy

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

run: $(EXECUTABLE)
	./$(EXECUTABLE) 80

debug: CFLAGS += -g -DDEBUG
debug: clean $(EXECUTABLE)

.PHONY: all clean run debug