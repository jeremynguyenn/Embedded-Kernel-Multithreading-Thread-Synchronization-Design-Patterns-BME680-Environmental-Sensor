#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "timer.h"
#include "logger.h"

struct timer {
    pthread_t thread;
    void (*callback)(void *);
    void *arg;
    long interval_ms;
    volatile int running;
};

static void timer_cleanup(void *arg) {
    struct timer *t = (struct timer *)arg;
    logger_log(LOG_INFO, "Timer cleanup: freeing resources");
    free(t);
}

static void *timer_task(void *arg) {
    struct timer *t = (struct timer *)arg;
    struct timespec ts;
    pthread_cleanup_push(timer_cleanup, t); // Cleanup if canceled
    while (t->running) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += t->interval_ms * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
        }
        pthread_testcancel();
        t->callback(t->arg);
        logger_log(LOG_DEBUG, "Timer callback executed");
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
    }
    pthread_cleanup_pop(0);
    return NULL;
}

int timer_init(timer_t *timer, long interval_ms, void (*callback)(void *), void *arg) {
    timer = malloc(sizeof(struct timer));
    if (!timer) {
        logger_log(LOG_ERROR, "Failed to allocate timer");
        return -ENOMEM;
    }
    timer->interval_ms = interval_ms;
    timer->callback = callback;
    timer->arg = arg;
    timer->running = 1;
    if (pthread_create(&timer->thread, NULL, timer_task, timer) != 0) {
        logger_log(LOG_ERROR, "Failed to create timer thread");
        free(timer);
        return -1;
    }
    logger_log(LOG_INFO, "Timer initialized with interval %ld ms", interval_ms);
    return 0;
}

void timer_destroy(timer_t *timer) {
    timer->running = 0;
    pthread_cancel(timer->thread);
    pthread_join(timer->thread, NULL);
    logger_log(LOG_INFO, "Timer destroyed");
}