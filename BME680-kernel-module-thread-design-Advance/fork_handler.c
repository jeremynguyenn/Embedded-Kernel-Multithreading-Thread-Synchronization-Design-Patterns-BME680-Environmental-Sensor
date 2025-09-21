#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include "fork_handler.h"
#include "logger.h"

struct fork_handler {
    pthread_t *threads;
    int num_threads;
    pthread_mutex_t mutex;
    volatile int running;
};

static void fork_cleanup(void *arg) {
    fork_handler_t *fh = (fork_handler_t *)arg;
    logger_log(LOG_INFO, "Fork cleanup: canceling other threads");
    for (int i = 0; i < fh->num_threads; i++) {
        if (fh->threads[i] != pthread_self()) {
            pthread_cancel(fh->threads[i]);
        }
    }
}

static void *worker_thread(void *arg) {
    fork_handler_t *fh = (fork_handler_t *)arg;
    while (fh->running) {
        logger_log(LOG_DEBUG, "Worker thread %lu running", (unsigned long)pthread_self());
        sleep(1);
    }
    return NULL;
}

int fork_handler_init(fork_handler_t *fh, int num_threads) {
    fh = malloc(sizeof(fork_handler_t));
    if (!fh) {
        logger_log(LOG_ERROR, "Failed to allocate fork handler");
        return -ENOMEM;
    }
    fh->threads = malloc(num_threads * sizeof(pthread_t));
    if (!fh->threads) {
        logger_log(LOG_ERROR, "Failed to allocate thread array");
        free(fh);
        return -ENOMEM;
    }
    pthread_mutex_init(&fh->mutex, NULL);
    fh->num_threads = num_threads;
    fh->running = 1;
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&fh->threads[i], NULL, worker_thread, fh) != 0) {
            logger_log(LOG_ERROR, "Failed to create thread %d", i);
            fork_handler_destroy(fh);
            return -1;
        }
    }
    logger_log(LOG_INFO, "Fork handler initialized with %d threads", num_threads);
    return 0;
}

void fork_handler_destroy(fork_handler_t *fh) {
    pthread_mutex_lock(&fh->mutex);
    fh->running = 0;
    pthread_mutex_unlock(&fh->mutex);
    for (int i = 0; i < fh->num_threads; i++) {
        pthread_join(fh->threads[i], NULL);
    }
    pthread_mutex_destroy(&fh->mutex);
    free(fh->threads);
    free(fh);
    logger_log(LOG_INFO, "Fork handler destroyed");
}

int fork_handler_fork(fork_handler_t *fh) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    if (pthread_mutex_timedlock(&fh->mutex, &ts) != 0) {
        logger_log(LOG_ERROR, "Timed out locking mutex for fork");
        return -ETIMEDOUT;
    }
    pthread_cleanup_push(fork_cleanup, fh); // Cleanup if canceled
    pid_t pid = fork();
    if (pid == -1) {
        pthread_cleanup_pop(0);
        pthread_mutex_unlock(&fh->mutex);
        logger_log(LOG_ERROR, "Fork failed: %s", strerror(errno));
        return -errno;
    }
    if (pid == 0) {
        // Child process
        logger_log(LOG_INFO, "Child process created with PID %d", getpid());
        // Cancel other threads
        for (int i = 0; i < fh->num_threads; i++) {
            if (fh->threads[i] != pthread_self()) {
                pthread_cancel(fh->threads[i]);
            }
        }
        pthread_cleanup_pop(0);
        pthread_mutex_unlock(&fh->mutex);
        // Child tasks
        sleep(2);
        logger_log(LOG_INFO, "Child process exiting");
        exit(0);
    } else {
        // Parent
        logger_log(LOG_INFO, "Parent process forked child with PID %d", pid);
        int status;
        waitpid(pid, &status, 0);
        pthread_cleanup_pop(0);
        pthread_mutex_unlock(&fh->mutex);
        logger_log(LOG_INFO, "Child process %d exited with status %d", pid, status);
    }
    return 0;
}