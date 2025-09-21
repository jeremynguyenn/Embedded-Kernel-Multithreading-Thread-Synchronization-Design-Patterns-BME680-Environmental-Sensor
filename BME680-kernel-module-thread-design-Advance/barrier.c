#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "barrier.h"
#include "logger.h"

struct barrier {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int total;
};

int barrier_init(barrier_t **barrier, int total) {
    if (total <= 0) {
        logger_log(LOG_ERROR, "Invalid barrier total: %d", total);
        return -EINVAL;
    }

    *barrier = malloc(sizeof(struct barrier));
    if (!*barrier) {
        logger_log(LOG_ERROR, "Failed to allocate barrier");
        return -ENOMEM;
    }

    pthread_mutex_init(&(*barrier)->mutex, NULL);
    pthread_cond_init(&(*barrier)->cond, NULL);
    (*barrier)->count = 0;
    (*barrier)->total = total;
    logger_log(LOG_INFO, "Barrier initialized for %d threads", total);
    return 0;
}

void barrier_destroy(barrier_t *barrier) {
    if (!barrier) return;

    pthread_mutex_destroy(&barrier->mutex);
    pthread_cond_destroy(&barrier->cond);
    free(barrier);
    logger_log(LOG_INFO, "Barrier destroyed");
}

int barrier_wait(barrier_t *barrier) {
    if (!barrier) {
        logger_log(LOG_ERROR, "Invalid barrier");
        return -EINVAL;
    }

    pthread_mutex_lock(&barrier->mutex);
    barrier->count++;
    if (barrier->count < barrier->total) {
        pthread_cond_wait(&barrier->cond, &barrier->mutex);
    } else {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
        logger_log(LOG_DEBUG, "Barrier released for %d threads", barrier->total);
    }
    pthread_mutex_unlock(&barrier->mutex);
    return 0;
}