TARGET_1 = queue-example
SRCS_1 = queue.c queue-example.c

TARGET_2 = queue-threads
SRCS_2 = queue.c queue-threads.c

CC=gcc
RM=rm
CFLAGS= -g -Wall
CFLAGSAN= -fsanitize=address -fsanitize=leak -Wall -Wextra -Werror -pthread
LIBS=-lpthread
INCLUDE_DIR="."

all: example example-san threads threads-san

example: ${SRCS_1} queue.h
	${CC} ${CFLAGS} -I${INCLUDE_DIR} ${SRCS_1} ${LIBS} -o $@

example-san: ${SRCS_1} queue.h
	${CC} ${CFLAGSAN} -I${INCLUDE_DIR} ${SRCS_1} ${LIBS} -o $@

threads: ${SRCS_2} queue.h
	${CC} ${CFLAGS} -I${INCLUDE_DIR} ${SRCS_2} ${LIBS} -o $@

threads-san: ${SRCS_2} queue.h
	${CC} ${CFLAGSAN} -I${INCLUDE_DIR} ${SRCS_2} ${LIBS} -o $@

clean:
	$(RM) example example-san threads threads-san