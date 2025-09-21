#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "recursive_mutex.h"
#include "logger.h"

struct recursive_mutex {
    pthread_mutex_t mutex;
    pthread_t owner;
    int count;
};

int recursive_mutex_init(recursive_mutex_t *rmutex) {
    rmutex = malloc(sizeof(struct recursive_mutex));
    if (!rmutex) {
        logger_log(LOG_ERROR, "Failed to allocate recursive mutex");
        return -ENOMEM;
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rmutex->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    rmutex->owner = 0;
    rmutex->count = 0;
    logger_log(LOG_INFO, "Recursive mutex initialized");
    return 0;
}

void recursive_mutex_destroy(recursive_mutex_t *rmutex) {
    pthread_mutex_destroy(&rmutex->mutex);
    free(rmutex);
    logger_log(LOG_INFO, "Recursive mutex destroyed");
}

int recursive_mutex_lock(recursive_mutex_t *rmutex) {
    pthread_t current = pthread_self();
    if (rmutex->owner == current) {
        rmutex->count++;
        logger_log(LOG_DEBUG, "Recursive mutex lock incremented: count=%d", rmutex->count);
        return 0;
    }
    int ret = pthread_mutex_lock(&rmutex->mutex);
    if (ret == 0) {
        rmutex->owner = current;
        rmutex->count = 1;
        logger_log(LOG_DEBUG, "Recursive mutex locked by thread %lu", (unsigned long)current);
    }
    return ret;
}

int recursive_mutex_unlock(recursive_mutex_t *rmutex) {
    pthread_t current = pthread_self();
    if (rmutex->owner != current) {
        logger_log(LOG_ERROR, "Attempt to unlock recursive mutex by non-owner thread");
        return -EPERM;
    }
    rmutex->count--;
    if (rmutex->count == 0) {
        rmutex->owner = 0;
        pthread_mutex_unlock(&rmutex->mutex);
        logger_log(LOG_DEBUG, "Recursive mutex fully unlocked");
    } else {
        logger_log(LOG_DEBUG, "Recursive mutex lock decremented: count=%d", rmutex->count);
    }
    return 0;
}