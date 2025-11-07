#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

void sigint_handler(int signo) {
    (void)signo;
    printf("Second thread: caught SIGINT (Ctrl+C)\n");
}

void* thread_block_all(void* arg) {
    (void)arg;
    printf("mythread [%d %d %d]: Hello from mythread block all!\n", getpid(), getppid(), gettid());
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    printf("First thread: all signals are blocked\n");
    while (1) {
        sleep(1);
    }
    return NULL;
}

void* thread_sigint_handler(void* arg) {
    (void)arg;
	printf("mythread [%d %d %d]: Hello from mythread sigint!\n", getpid(), getppid(), gettid());
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        pthread_exit(NULL);
    }

    printf("Second thread: waiting for SIGINT (Ctrl+C)\n");

    while (1) {
        pause();
    }

    return NULL;
}

void* thread_sigquit_wait(void* arg) {
    (void)arg;
	printf("mythread [%d %d %d]: Hello from mythread sigquit!\n", getpid(), getppid(), gettid());
    sigset_t set;
    int sig;

    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);

    pthread_sigmask(SIG_BLOCK, &set, NULL);

    printf("Third thread: waiting for SIGQUIT (Ctrl+\\)\n");

    while (1) {
        if (sigwait(&set, &sig) == 0) {
            if (sig == SIGQUIT) {
                printf("Third thread: caught SIGQUIT (Ctrl+\\)\n");
                break;
            }
        }
    }

    return NULL;
}

int main() {
    pthread_t t1, t2, t3;
    int err;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    err = pthread_create(&t1, NULL, thread_block_all, NULL);
    if (err) {
	    printf("main: pthread_create() failed: %s\n", strerror(err));
		return 1;
	}
    err = pthread_create(&t2, NULL, thread_sigint_handler, NULL);
    if (err) {
	    printf("main: pthread_create() failed: %s\n", strerror(err));
		return 1;
	}
    err = pthread_create(&t3, NULL, thread_sigquit_wait, NULL);
    if (err) {
	    printf("main: pthread_create() failed: %s\n", strerror(err));
		return 1;
	}
    err = pthread_join(t1, NULL);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return -1;
    }
    err = pthread_join(t2, NULL);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return -1;
    }
    err = pthread_join(t3, NULL);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return -1;
    }

    return 0;
}