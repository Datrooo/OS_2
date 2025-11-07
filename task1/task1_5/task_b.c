#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void handler_A(int sig) {
    (void)sig;
    write(1, "Handler A win\n", 15);
}

void handler_B(int sig) {
    (void)sig;
    write(1, "Handler B wins\n", 16);
}

static void *t2_set_A(void *arg) {
    (void)arg;
    sigset_t m; sigemptyset(&m); sigaddset(&m, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &m, NULL);

    struct sigaction sa = {0};
    sa.sa_handler = handler_A;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        pthread_exit(NULL);
    }
    write(1, "T2: installed handler A for SIGUSR1\n", 36);

    for (;;) pause();
    return NULL;
}

static void *t3_set_B(void *arg) {
    (void)arg;
    sigset_t m; sigemptyset(&m); sigaddset(&m, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &m, NULL);

    sleep(1);
    struct sigaction sb = {0};
    sb.sa_handler = handler_B;
    sigemptyset(&sb.sa_mask);
    if (sigaction(SIGUSR1, &sb, NULL) == -1) {
        perror("sigaction");
        pthread_exit(NULL);
    }
    write(1, "T3: installed handler B for SIGUSR1 (overrides A)\n", 51);

    for (;;) pause();
    return NULL;
}

int main(void) {
    printf("PID is %d", getpid());
    sigset_t all; 
    sigemptyset(&all); 
    sigaddset(&all, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &all, NULL);

    pthread_t t2, t3;
    int err;
    err = pthread_create(&t2, NULL, t2_set_A, NULL);
    if (err) {
	    printf("main: pthread_create() failed: %s\n", strerror(err));
		return 1;
	}
    err = pthread_create(&t3, NULL, t3_set_B, NULL);
    if (err) {
	    printf("main: pthread_create() failed: %s\n", strerror(err));
		return 1;
	}

    sleep(2);

    write(1, "main: sending SIGUSR1 to T2\n", 28);
    err = pthread_kill(t2, SIGUSR1);
    if (err) {
        printf("main: failed to send signal\n");
        return 1;
    }
    sleep(1);
    write(1, "main: sending SIGUSR1 to T3\n", 28);
    err = pthread_kill(t3, SIGUSR1);
    if (err) {
        printf("main: failed to send signal\n");
        return 1;
    }
}
