#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>

struct myStruct {
	int integer;
	char* string;
};

void* mythread_A(void* arg) {
	struct myStruct* ms = (struct myStruct*)arg;
	printf("Integer: %d\n", ms->integer);
	printf("String: %s\n", ms->string);
	return NULL;
}

void* mythread_B(void* arg) {
	struct myStruct* ms = (struct myStruct*)arg;
	printf("Integer: %d, String: %s\n", ms->integer, ms->string);
	if (ms->integer < 0) {
		free(ms);
	}
	return NULL;
}

struct myStruct ms_global = {
	.integer = 10,
	.string = "Global"
};

int main(int argc, char** argv) {
	pthread_t tids[4];
	int err;


	struct myStruct* ms_heap = malloc(sizeof(struct myStruct));
	ms_heap->integer = -20,
	ms_heap->string = "Heap";

	static struct myStruct ms_static = {
		.integer = 25,
		.string = "Static"
	};

	if (argc < 2) {
		printf("Not enough arguments!\n");
	}
	else if (strcmp(argv[1], "a") == 0) {
		struct myStruct ms_local = {
		.integer = 15,
		.string = "Local"
		};

		err = pthread_create(&tids[0], NULL, mythread_A, &ms_local);
		if (err) {
			printf("Main: pthread_create() failed: %s\n", strerror(err));
			return -1;
		}

		pthread_join(tids[0], NULL);
	}
	else if (strcmp(argv[1], "b") == 0) {
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

		err = pthread_create(&tids[0], &attr, mythread_B, &ms_global);
		if (err) {
			printf("Main: pthread_create() failed: %s\n", strerror(err));
			pthread_attr_destroy(&attr);
			return -1;
		}

		err = pthread_create(&tids[1], &attr, mythread_B, &ms_static);
                if (err) {
                        printf("Main: pthread_create() failed: %s\n", strerror(err));
			pthread_attr_destroy(&attr);
                        return -1;
                }

		err = pthread_create(&tids[2], &attr, mythread_B, ms_heap);
                if (err) {
                        printf("Main: pthread_create() failed: %s\n", strerror(err));
			pthread_attr_destroy(&attr);
                        return -1;
                }

		struct myStruct ms_local = {
		.integer = 15,
		.string = "Local"
		};

		err = pthread_create(&tids[3], &attr, mythread_B, &ms_local);
                if (err) {
                        printf("Main: pthread_create() failed: %s\n", strerror(err));
						pthread_attr_destroy(&attr);
                        return -1;
                }
		pthread_attr_destroy(&attr);
		//sleep(5);
	}
	else {
		printf("Wrong argument!\n");
	}

	pthread_exit(NULL);
}
