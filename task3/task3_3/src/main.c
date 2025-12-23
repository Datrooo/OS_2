#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "loop.h"
#include "cache.h"
#include "session.h"
#include "parse.h"
#include "key_builder.h"
#include "downloader.h"
#include "gc.h"
#include "net.h"
#include "dirty.h"

#define LISTEN_PORT 80
#define MAX_CACHE_SIZE (512 * 1024 * 1024)  // 512 MB 
#define DOWNLOADER_POOL_SIZE 4
#define VERSION "0.1"

void print_usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [port]\n", argv0);
}

void print_config(const char *bind_ip, int port) {
    fprintf(stdout, "Proxy Server Version %s\n", VERSION);
    fprintf(stdout, "Bind Address: %s\n", bind_ip);
    fprintf(stdout, "Listen Port: %d\n", port);
    fprintf(stdout, "Max Cache Size: %.1f MB\n", MAX_CACHE_SIZE / 1024.0 / 1024.0);
    fprintf(stdout, "Downloader Pool Size: %d threads\n", DOWNLOADER_POOL_SIZE);
    fprintf(stdout, "\n");
}

int main(int argc, char *argv[]) {
    int port = LISTEN_PORT;
    const char *bind_ip = "0.0.0.0";

    int cache_inited = 0;
    int dirty_inited = 0;
    int downloader_inited = 0;
    int gc_inited = 0;
    int loop_inited = 0;
    int exit_code = 1;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            port = atoi(argv[i]);
            continue;
        }
        fprintf(stderr, "Unknown arg: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    print_config(bind_ip, port);

    do {
        if (cache_init(MAX_CACHE_SIZE) != 0) {
            fprintf(stderr, "Failed to initialize cache\n");
            break;
        }
        cache_inited = 1;
        fprintf(stdout, "Cache initialized (%.1f MB)\n", MAX_CACHE_SIZE / 1024.0 / 1024.0);

        if (dirty_init() != 0) {
            fprintf(stderr, "Failed to initialize dirty queue\n");
            break;
        }
        dirty_inited = 1;
        fprintf(stdout, "Dirty queue initialized\n");

        if (downloader_init() != 0) {
            fprintf(stderr, "Failed to initialize downloader pool\n");
            break;
        }
        downloader_inited = 1;
        fprintf(stdout, "Downloader pool initialized (%d threads)\n", DOWNLOADER_POOL_SIZE);

        if (gc_init(MAX_CACHE_SIZE) != 0) {
            fprintf(stderr, "Failed to initialize GC\n");
            break;
        }
        gc_inited = 1;
        fprintf(stdout, "GC (Garbage Collector) initialized\n");

        if (loop_init(bind_ip, port) != 0) {
            fprintf(stderr, "Failed to initialize event loop\n");
            break;
        }
        loop_inited = 1;
        exit_code = 0;
        fprintf(stdout, "Event loop initialized\n\n");
    } while (0);
    
    signal(SIGPIPE, SIG_IGN);
    
    if (loop_inited) {
        loop_run();
    }
    
    if (loop_inited) {
        loop_shutdown();
        loop_inited = 0;
    }

    if (downloader_inited) {
        downloader_shutdown();
        downloader_inited = 0;
    }

    if (gc_inited) {
        gc_shutdown();
        gc_inited = 0;
    }

    if (dirty_inited) {
        dirty_cleanup();
        dirty_inited = 0;
    }

    if (cache_inited) {
        cache_cleanup();
        cache_inited = 0;
    }
    
    fprintf(stdout, "Proxy stopped!\n");
    
    return exit_code;
}