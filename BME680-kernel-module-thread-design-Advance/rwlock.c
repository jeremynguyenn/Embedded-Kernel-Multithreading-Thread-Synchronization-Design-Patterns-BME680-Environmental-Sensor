#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "rwlock.h"
#include "logger.h"

#define MAX_WAITING_READERS 10
#define MAX_WAITING_WRITERS 5

struct rwlock {
    pthread_mutex_t mutex;
    pthread_cond_t readers_proceed;
    pthread_cond_t writer_proceed;
    int active_readers;
    int active_writers;
    int waiting_readers;
    int waiting_writers;
};

int rwlock_init(rwlock_t *rwlock) {
    rwlock = malloc(sizeof(struct rwlock));
    if (!rwlock) {
        logger_log(LOG_ERROR, "Failed to allocate rwlock");
        return -ENOMEM;
    }
    pthread_mutex_init(&rwlock->mutex, NULL);
    pthread_cond_init(&rwlock->readers_proceed, NULL);
    pthread_cond_init(&rwlock->writer_proceed, NULL);
    rwlock->active_readers = 0;
    rwlock->active_writers = 0;
    rwlock->waiting_readers = 0;
    rwlock->waiting_writers = 0;
    logger_log(LOG_INFO, "Read-write lock initialized");
    return 0;
}

void rwlock_destroy(rwlock_t *rwlock) {
    pthread_mutex_destroy(&rwlock->mutex);
    pthread_cond_destroy(&rwlock->readers_proceed);
    pthread_cond_destroy(&rwlock->writer_proceed);
    free(rwlock);
    logger_log(LOG_INFO, "Read-write lock destroyed");
}

int rwlock_rdlock(rwlock_t *rwlock) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // 5-second timeout

    pthread_mutex_lock(&rwlock->mutex);
    if (rwlock->waiting_readers >= MAX_WAITING_READERS) {
        pthread_mutex_unlock(&rwlock->mutex);
        logger_log(LOG_ERROR, "Too many waiting readers: %d", rwlock->waiting_readers);
        return -EAGAIN;
    }
    rwlock->waiting_readers++;
    while (rwlock->active_writers || rwlock->waiting_writers) {
        int ret = pthread_cond_timedwait(&rwlock->readers_proceed, &rwlock->mutex, &ts);
        if (ret == ETIMEDOUT) {
            rwlock->waiting_readers--;
            pthread_mutex_unlock(&rwlock->mutex);
            logger_log(LOG_ERROR, "Read lock timed out");
            return -ETIMEDOUT;
        }
        if (ret != 0) {
            rwlock->waiting_readers--;
            pthread_mutex_unlock(&rwlock->mutex);
            logger_log(LOG_ERROR, "Read lock wait failed: %s", strerror(ret));
            return ret;
        }
    }
    rwlock->waiting_readers--;
    rwlock->active_readers++;
    if (rwlock->active_readers < 0 || rwlock->active_writers < 0) {
        pthread_mutex_unlock(&rwlock->mutex);
        logger_log(LOG_ERROR, "Invalid rwlock state: readers=%d, writers=%d", rwlock->active_readers, rwlock->active_writers);
        return -EINVAL;
    }
    pthread_mutex_unlock(&rwlock->mutex);
    logger_log(LOG_DEBUG, "Read lock acquired");
    return 0;
}

int rwlock_wrlock(rwlock_t *rwlock) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // 5-second timeout

    pthread_mutex_lock(&rwlock->mutex);
    if (rwlock->waiting_writers >= MAX_WAITING_WRITERS) {
        pthread_mutex_unlock(&rwlock->mutex);
        logger_log(LOG_ERROR, "Too many waiting writers: %d", rwlock->waiting_writers);
        return -EAGAIN;
    }
    rwlock->waiting_writers++;
    while (rwlock->active_readers || rwlock->active_writers) {
        int ret = pthread_cond_timedwait(&rwlock->writer_proceed, &rwlock->mutex, &ts);
        if (ret == ETIMEDOUT) {
            rwlock->waiting_writers--;
            pthread_mutex_unlock(&rwlock->mutex);
            logger_log(LOG_ERROR, "Write lock timed out");
            return -ETIMEDOUT;
        }
        if (ret != 0) {
            rwlock->waiting_writers--;
            pthread_mutex_unlock(&rwlock->mutex);
            logger_log(LOG_ERROR, "Write lock wait failed: %s", strerror(ret));
            return ret;
        }
    }
    rwlock->waiting_writers--;
    rwlock->active_writers++;
    if (rwlock->active_readers < 0 || rwlock->active_writers < 0) {
        pthread_mutex_unlock(&rwlock->mutex);
        logger_log(LOG_ERROR, "Invalid rwlock state: readers=%d, writers=%d", rwlock->active_readers, rwlock->active_writers);
        return -EINVAL;
    }
    pthread_mutex_unlock(&rwlock->mutex);
    logger_log(LOG_DEBUG, "Write lock acquired");
    return 0;
}

int rwlock_unlock(rwlock_t *rwlock) {
    pthread_mutex_lock(&rwlock->mutex);
    if (rwlock->active_writers) {
        rwlock->active_writers--;
        if (rwlock->waiting_writers) {
            pthread_cond_signal(&rwlock->writer_proceed);
        } else {
            pthread_cond_broadcast(&rwlock->readers_proceed);
        }
    } else if (rwlock->active_readers) {
        rwlock->active_readers--;
        if (rwlock->active_readers == 0 && rwlock->waiting_writers) {
            pthread_cond_signal(&rwlock->writer_proceed);
        }
    } else {
        pthread_mutex_unlock(&rwlock->mutex);
        logger_log(LOG_ERROR, "Attempt to unlock rwlock with no active readers or writers");
        return -EPERM;
    }
    if (rwlock->active_readers < 0 || rwlock->active_writers < 0) {
        pthread_mutex_unlock(&rwlock->mutex);
        logger_log(LOG_ERROR, "Invalid rwlock state: readers=%d, writers=%d", rwlock->active_readers, rwlock->active_writers);
        return -EINVAL;
    }
    pthread_mutex_unlock(&rwlock->mutex);
    logger_log(LOG_DEBUG, "Lock released");
    return 0;
}