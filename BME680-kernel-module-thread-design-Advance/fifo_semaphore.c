#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "fifo_semaphore.h"
#include "logger.h"

struct fifo_semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
};

int fifo_semaphore_init(fifo_semaphore_t *sem, int value) {
    sem = malloc(sizeof(struct fifo_semaphore));
    if (!sem) {
        logger_log(LOG_ERROR, "Failed to allocate FIFO semaphore");
        return -ENOMEM;
    }
    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->cond, NULL);
    if (value < 0) {
        logger_log(LOG_ERROR, "Invalid initial semaphore value: %d", value);
        free(sem);
        return -EINVAL;
    }
    sem->count = value;
    logger_log(LOG_INFO, "FIFO semaphore initialized with value %d", value);
    return 0;
}

void fifo_semaphore_destroy(fifo_semaphore_t *sem) {
    pthread_mutex_destroy(&sem->mutex);
    pthread_cond_destroy(&sem->cond);
    free(sem);
    logger_log(LOG_INFO, "FIFO semaphore destroyed");
}

int fifo_semaphore_wait(fifo_semaphore_t *sem) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // 5-second timeout

    pthread_mutex_lock(&sem->mutex);
    while (sem->count <= 0) {
        int ret = pthread_cond_timedwait(&sem->cond, &sem->mutex, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&sem->mutex);
            logger_log(LOG_ERROR, "FIFO semaphore wait timed out");
            return -ETIMEDOUT;
        }
        if (ret != 0) {
            pthread_mutex_unlock(&sem->mutex);
            logger_log(LOG_ERROR, "FIFO semaphore wait failed: %s", strerror(ret));
            return ret;
        }
    }
    if (sem->count < 0) {
        pthread_mutex_unlock(&sem->mutex);
        logger_log(LOG_ERROR, "Invalid semaphore count: %d", sem->count);
        return -EINVAL;
    }
    sem->count--;
    pthread_mutex_unlock(&sem->mutex);
    logger_log(LOG_DEBUG, "FIFO semaphore acquired, count=%d", sem->count);
    return 0;
}

int fifo_semaphore_post(fifo_semaphore_t *sem) {
    pthread_mutex_lock(&sem->mutex);
    sem->count++;
    if (sem->count < 0) {
        pthread_mutex_unlock(&sem->mutex);
        logger_log(LOG_ERROR, "Invalid semaphore count: %d", sem->count);
        return -EINVAL;
    }
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->mutex);
    logger_log(LOG_DEBUG, "FIFO semaphore released, count=%d", sem->count);
    return 0;
}