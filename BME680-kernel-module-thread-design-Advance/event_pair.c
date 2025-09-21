#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "event_pair.h"
#include "logger.h"

struct event_pair {
    pthread_mutex_t mutex;
    pthread_cond_t cond1;
    pthread_cond_t cond2;
    int state; // 0: idle, 1: signal1, 2: signal2
};

static void event_cleanup(void *arg) {
    struct event_pair *ep = (struct event_pair *)arg;
    logger_log(LOG_INFO, "Event pair cleanup: destroying conds");
    pthread_cond_destroy(&ep->cond1);
    pthread_cond_destroy(&ep->cond2);
}

int event_pair_init(event_pair_t **ep) {
    *ep = malloc(sizeof(struct event_pair));
    if (!*ep) return -ENOMEM;
    pthread_mutex_init(&(*ep)->mutex, NULL);
    pthread_cond_init(&(*ep)->cond1, NULL);
    pthread_cond_init(&(*ep)->cond2, NULL);
    (*ep)->state = 0;
    return 0;
}

void event_pair_destroy(event_pair_t *ep) {
    pthread_mutex_destroy(&ep->mutex);
    pthread_cond_destroy(&ep->cond1);
    pthread_cond_destroy(&ep->cond2);
    free(ep);
}

int event_pair_signal1(event_pair_t *ep) {
    pthread_mutex_lock(&ep->mutex);
    ep->state = 1;
    pthread_cond_signal(&ep->cond1);
    pthread_mutex_unlock(&ep->mutex);
    return 0;
}

int event_pair_wait2(event_pair_t *ep) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    pthread_cleanup_push(event_cleanup, ep);
    pthread_mutex_lock(&ep->mutex);
    while (ep->state != 2) {
        if (pthread_cond_timedwait(&ep->cond2, &ep->mutex, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&ep->mutex);
            pthread_cleanup_pop(0);
            return -ETIMEDOUT;
        }
    }
    ep->state = 0;
    pthread_mutex_unlock(&ep->mutex);
    pthread_cleanup_pop(0);
    return 0;
}

// Tương tự cho signal2 và wait1
int event_pair_signal2(event_pair_t *ep) {
    pthread_mutex_lock(&ep->mutex);
    ep->state = 2;
    pthread_cond_signal(&ep->cond2);
    pthread_mutex_unlock(&ep->mutex);
    return 0;
}

int event_pair_wait1(event_pair_t *ep) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    pthread_cleanup_push(event_cleanup, ep);
    pthread_mutex_lock(&ep->mutex);
    while (ep->state != 1) {
        if (pthread_cond_timedwait(&ep->cond1, &ep->mutex, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&ep->mutex);
            pthread_cleanup_pop(0);
            return -ETIMEDOUT;
        }
    }
    ep->state = 0;
    pthread_mutex_unlock(&ep->mutex);
    pthread_cleanup_pop(0);
    return 0;
}