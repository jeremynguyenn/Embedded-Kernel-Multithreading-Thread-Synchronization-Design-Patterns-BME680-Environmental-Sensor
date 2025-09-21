#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "dining_philosophers.h"
#include "logger.h"

struct dining_philosophers {
    int num_philosophers;
    pthread_mutex_t *forks;
    volatile int running;
};

int dining_philosophers_init(dining_philosophers_t **dp, int num_philosophers) {
    if (num_philosophers <= 0) {
        logger_log(LOG_ERROR, "Invalid number of philosophers: %d", num_philosophers);
        return -EINVAL;
    }

    *dp = malloc(sizeof(struct dining_philosophers));
    if (!*dp) {
        logger_log(LOG_ERROR, "Failed to allocate dining philosophers");
        return -ENOMEM;
    }

    (*dp)->num_philosophers = num_philosophers;
    (*dp)->running = 1;
    (*dp)->forks = malloc(num_philosophers * sizeof(pthread_mutex_t));
    if (!(*dp)->forks) {
        logger_log(LOG_ERROR, "Failed to allocate forks");
        free(*dp);
        return -ENOMEM;
    }

    for (int i = 0; i < num_philosophers; i++) {
        if (pthread_mutex_init(&(*dp)->forks[i], NULL) != 0) {
            logger_log(LOG_ERROR, "Failed to initialize fork %d", i);
            for (int j = 0; j < i; j++) {
                pthread_mutex_destroy(&(*dp)->forks[j]);
            }
            free((*dp)->forks);
            free(*dp);
            return -errno;
        }
    }

    logger_log(LOG_INFO, "Dining philosophers initialized with %d philosophers", num_philosophers);
    return 0;
}

void dining_philosophers_destroy(dining_philosophers_t *dp) {
    if (!dp) return;

    dp->running = 0;
    for (int i = 0; i < dp->num_philosophers; i++) {
        pthread_mutex_destroy(&dp->forks[i]);
    }
    free(dp->forks);
    free(dp);
    logger_log(LOG_INFO, "Dining philosophers destroyed");
}

int dining_philosophers_think(dining_philosophers_t *dp, int id) {
    if (!dp || id < 0 || id >= dp->num_philosophers) {
        logger_log(LOG_ERROR, "Invalid philosopher id: %d", id);
        return -EINVAL;
    }
    if (!dp->running) {
        logger_log(LOG_WARNING, "Dining philosophers not running");
        return -EAGAIN;
    }
    logger_log(LOG_DEBUG, "Philosopher %d is thinking", id);
    return 0;
}

int dining_philosophers_eat(dining_philosophers_t *dp, int id) {
    if (!dp || id < 0 || id >= dp->num_philosophers) {
        logger_log(LOG_ERROR, "Invalid philosopher id: %d", id);
        return -EINVAL;
    }
    if (!dp->running) {
        logger_log(LOG_WARNING, "Dining philosophers not running");
        return -EAGAIN;
    }

    // Pick up forks in order to avoid deadlock
    if (id % 2 == 0) {
        pthread_mutex_lock(&dp->forks[id]);
        pthread_mutex_lock(&dp->forks[(id + 1) % dp->num_philosophers]);
    } else {
        pthread_mutex_lock(&dp->forks[(id + 1) % dp->num_philosophers]);
        pthread_mutex_lock(&dp->forks[id]);
    }
    logger_log(LOG_DEBUG, "Philosopher %d is eating", id);
    return 0;
}

int dining_philosophers_done(dining_philosophers_t *dp, int id) {
    if (!dp || id < 0 || id >= dp->num_philosophers) {
        logger_log(LOG_ERROR, "Invalid philosopher id: %d", id);
        return -EINVAL;
    }
    if (!dp->running) {
        logger_log(LOG_WARNING, "Dining philosophers not running");
        return -EAGAIN;
    }

    pthread_mutex_unlock(&dp->forks[id]);
    pthread_mutex_unlock(&dp->forks[(id + 1) % dp->num_philosophers]);
    logger_log(LOG_DEBUG, "Philosopher %d done eating", id);
    return 0;
}