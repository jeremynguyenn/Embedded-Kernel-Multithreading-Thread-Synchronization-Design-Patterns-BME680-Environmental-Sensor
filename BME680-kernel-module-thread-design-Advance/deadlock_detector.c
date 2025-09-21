#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "deadlock_detector.h"
#include "logger.h"

struct lock_info {
    pthread_t owner;
    int locked;
};

struct deadlock_detector {
    struct lock_info *locks;
    int num_mutexes;
    pthread_mutex_t mutex;
};

int deadlock_detector_init(deadlock_detector_t *dd, int num_mutexes) {
    dd = malloc(sizeof(deadlock_detector_t));
    if (!dd) {
        logger_log(LOG_ERROR, "Failed to allocate deadlock detector");
        return -ENOMEM;
    }
    dd->locks = malloc(num_mutexes * sizeof(struct lock_info));
    if (!dd->locks) {
        logger_log(LOG_ERROR, "Failed to allocate lock info array");
        free(dd);
        return -ENOMEM;
    }
    dd->num_mutexes = num_mutexes;
    for (int i = 0; i < num_mutexes; i++) {
        dd->locks[i].owner = 0;
        dd->locks[i].locked = 0;
    }
    pthread_mutex_init(&dd->mutex, NULL);
    logger_log(LOG_INFO, "Deadlock detector initialized with %d mutexes", num_mutexes);
    return 0;
}

void deadlock_detector_destroy(deadlock_detector_t *dd) {
    pthread_mutex_destroy(&dd->mutex);
    free(dd->locks);
    free(dd);
    logger_log(LOG_INFO, "Deadlock detector destroyed");
}

int deadlock_detector_lock(deadlock_detector_t *dd, int mutex_id) {
    if (mutex_id < 0 || mutex_id >= dd->num_mutexes) {
        logger_log(LOG_ERROR, "Invalid mutex ID: %d", mutex_id);
        return -EINVAL;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    if (pthread_mutex_timedlock(&dd->mutex, &ts) != 0) return -ETIMEDOUT;
    pthread_t current = pthread_self();
    for (int i = 0; i < dd->num_mutexes; i++) {
        if (dd->locks[i].locked && dd->locks[i].owner != current) {
            for (int j = 0; j < dd->num_mutexes; j++) {
                if (dd->locks[j].locked && dd->locks[j].owner == current && j > i) {
                    logger_log(LOG_WARNING, "Potential deadlock detected: thread %lu holds mutex %d, wants mutex %d",
                               (unsigned long)current, j, i);
                    pthread_mutex_unlock(&dd->mutex);
                    usleep(10000); // Backoff
                    return -EDEADLK;
                }
            }
        }
    }
    dd->locks[mutex_id].owner = current;
    dd->locks[mutex_id].locked = 1;
    pthread_mutex_unlock(&dd->mutex);
    logger_log(LOG_DEBUG, "Mutex %d locked by thread %lu", mutex_id, (unsigned long)current);
    return 0;
}

int deadlock_detector_unlock(deadlock_detector_t *dd, int mutex_id) {
    if (mutex_id < 0 || mutex_id >= dd->num_mutexes) {
        logger_log(LOG_ERROR, "Invalid mutex ID: %d", mutex_id);
        return -EINVAL;
    }
    pthread_mutex_lock(&dd->mutex);
    if (!dd->locks[mutex_id].locked || dd->locks[mutex_id].owner != pthread_self()) {
        pthread_mutex_unlock(&dd->mutex);
        logger_log(LOG_ERROR, "Attempt to unlock mutex %d by non-owner thread", mutex_id);
        return -EPERM;
    }
    dd->locks[mutex_id].locked = 0;
    dd->locks[mutex_id].owner = 0;
    pthread_mutex_unlock(&dd->mutex);
    logger_log(LOG_DEBUG, "Mutex %d unlocked", mutex_id);
    return 0;
}