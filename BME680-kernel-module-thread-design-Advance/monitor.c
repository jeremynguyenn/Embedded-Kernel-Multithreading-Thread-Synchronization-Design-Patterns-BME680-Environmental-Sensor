#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "monitor.h"
#include "logger.h"
#include "rwlock.h"
#include "deadlock_detector.h"

struct bme680_monitor {
    struct bme680_fifo_data *data;
    int head;
    int tail;
    int size;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    rwlock_t rwlock;
    deadlock_detector_t *dd;
};

int bme680_monitor_init(struct bme680_monitor **monitor, int size) {
    *monitor = malloc(sizeof(struct bme680_monitor));
    if (!*monitor) {
        logger_log(LOG_ERROR, "Failed to allocate monitor");
        return -ENOMEM;
    }
    (*monitor)->data = malloc(size * sizeof(struct bme680_fifo_data));
    if (!(*monitor)->data) {
        logger_log(LOG_ERROR, "Failed to allocate monitor data");
        free(*monitor);
        return -ENOMEM;
    }
    (*monitor)->size = size;
    (*monitor)->head = 0;
    (*monitor)->tail = 0;
    (*monitor)->count = 0;
    pthread_mutex_init(&(*monitor)->mutex, NULL);
    pthread_cond_init(&(*monitor)->not_full, NULL);
    pthread_cond_init(&(*monitor)->not_empty, NULL);
    rwlock_init(&(*monitor)->rwlock);
    deadlock_detector_init(&(*monitor)->dd, 2); // 2 mutexes: mutex and rwlock
    logger_log(LOG_INFO, "Monitor initialized with size %d", size);
    return 0;
}

void bme680_monitor_destroy(struct bme680_monitor *monitor) {
    rwlock_wrlock(&monitor->rwlock);
    free(monitor->data);
    pthread_mutex_destroy(&monitor->mutex);
    pthread_cond_destroy(&monitor->not_full);
    pthread_cond_destroy(&monitor->not_empty);
    rwlock_destroy(&monitor->rwlock);
    deadlock_detector_destroy(monitor->dd);
    free(monitor);
    logger_log(LOG_INFO, "Monitor destroyed");
}

int bme680_monitor_write(struct bme680_monitor *monitor, struct bme680_fifo_data *data) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // 5-second timeout

    if (data->temp < -40 || data->temp > 85 || data->pressure < 30000 || data->pressure > 110000 || data->humidity < 0 || data->humidity > 100) {
        logger_log(LOG_ERROR, "Invalid sensor data: temp=%f, pressure=%u, humidity=%u", data->temp, data->pressure, data->humidity);
        return -EINVAL;
    }

    if (deadlock_detector_lock(monitor->dd, 0) != 0) {
        logger_log(LOG_ERROR, "Potential deadlock detected during monitor write");
        return -EDEADLK;
    }
    pthread_mutex_lock(&monitor->mutex);
    while (monitor->count == monitor->size) {
        int ret = pthread_cond_timedwait(&monitor->not_full, &monitor->mutex, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&monitor->mutex);
            deadlock_detector_unlock(monitor->dd, 0);
            logger_log(LOG_ERROR, "Monitor write timed out");
            return -ETIMEDOUT;
        }
        if (ret != 0) {
            pthread_mutex_unlock(&monitor->mutex);
            deadlock_detector_unlock(monitor->dd, 0);
            logger_log(LOG_ERROR, "Monitor write wait failed: %s", strerror(ret));
            return ret;
        }
    }
    if (monitor->count < 0 || monitor->count > monitor->size) {
        pthread_mutex_unlock(&monitor->mutex);
        deadlock_detector_unlock(monitor->dd, 0);
        logger_log(LOG_ERROR, "Invalid monitor count: %d", monitor->count);
        return -EINVAL;
    }
    rwlock_wrlock(&monitor->rwlock);
    monitor->data[monitor->tail] = *data;
    monitor->tail = (monitor->tail + 1) % monitor->size;
    monitor->count++;
    pthread_cond_signal(&monitor->not_empty);
    rwlock_unlock(&monitor->rwlock);
    pthread_mutex_unlock(&monitor->mutex);
    deadlock_detector_unlock(monitor->dd, 0);
    logger_log(LOG_DEBUG, "Monitor write: count=%d", monitor->count);
    return 0;
}

int bme680_monitor_read(struct bme680_monitor *monitor, struct bme680_fifo_data *data) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // 5-second timeout

    if (deadlock_detector_lock(monitor->dd, 1) != 0) {
        logger_log(LOG_ERROR, "Potential deadlock detected during monitor read");
        return -EDEADLK;
    }
    pthread_mutex_lock(&monitor->mutex);
    while (monitor->count == 0) {
        int ret = pthread_cond_timedwait(&monitor->not_empty, &monitor->mutex, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&monitor->mutex);
            deadlock_detector_unlock(monitor->dd, 1);
            logger_log(LOG_ERROR, "Monitor read timed out");
            return -ETIMEDOUT;
        }
        if (ret != 0) {
            pthread_mutex_unlock(&monitor->mutex);
            deadlock_detector_unlock(monitor->dd, 1);
            logger_log(LOG_ERROR, "Monitor read wait failed: %s", strerror(ret));
            return ret;
        }
    }
    if (monitor->count < 0 || monitor->count > monitor->size) {
        pthread_mutex_unlock(&monitor->mutex);
        deadlock_detector_unlock(monitor->dd, 1);
        logger_log(LOG_ERROR, "Invalid monitor count: %d", monitor->count);
        return -EINVAL;
    }
    rwlock_rdlock(&monitor->rwlock);
    *data = monitor->data[monitor->head];
    monitor->head = (monitor->head + 1) % monitor->size;
    monitor->count--;
    pthread_cond_signal(&monitor->not_full);
    rwlock_unlock(&monitor->rwlock);
    pthread_mutex_unlock(&monitor->mutex);
    deadlock_detector_unlock(monitor->dd, 1);
    logger_log(LOG_DEBUG, "Monitor read: count=%d", monitor->count);
    return 0;
}