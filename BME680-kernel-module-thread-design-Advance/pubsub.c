#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "pubsub.h"
#include "logger.h"
#include "event_pair.h"

struct subscriber {
    char *topic;
    void (*callback)(void *, size_t);
    struct subscriber *next;
};

struct pubsub {
    struct subscriber *subscribers;
    pthread_mutex_t mutex;
    event_pair_t *ep;
};

static struct pubsub ps;

void pubsub_init(void) {
    pthread_mutex_init(&ps.mutex, NULL);
    event_pair_init(&ps.ep);
    ps.subscribers = NULL;
    logger_log(LOG_INFO, "Pubsub initialized");
}

void pubsub_destroy(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    if (pthread_mutex_timedlock(&ps.mutex, &ts) != 0) {
        logger_log(LOG_ERROR, "Timed out destroying pubsub");
        return;
    }
    struct subscriber *sub = ps.subscribers;
    while (sub) {
        struct subscriber *next = sub->next;
        free(sub->topic);
        free(sub);
        sub = next;
    }
    pthread_mutex_unlock(&ps.mutex);
    pthread_mutex_destroy(&ps.mutex);
    event_pair_destroy(&ps.ep);
    logger_log(LOG_INFO, "Pubsub destroyed");
}

int pubsub_subscribe(const char *topic, void (*callback)(void *, size_t)) {
    if (!topic || !callback) {
        logger_log(LOG_ERROR, "Invalid topic or callback");
        return -EINVAL;
    }
    struct subscriber *sub = malloc(sizeof(struct subscriber));
    if (!sub) {
        logger_log(LOG_ERROR, "Failed to allocate subscriber");
        return -ENOMEM;
    }
    sub->topic = strdup(topic);
    if (!sub->topic) {
        logger_log(LOG_ERROR, "Failed to allocate topic");
        free(sub);
        return -ENOMEM;
    }
    sub->callback = callback;
    sub->next = NULL;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    if (pthread_mutex_timedlock(&ps.mutex, &ts) != 0) return -ETIMEDOUT;
    sub->next = ps.subscribers;
    ps.subscribers = sub;
    pthread_mutex_unlock(&ps.mutex);
    logger_log(LOG_INFO, "Subscribed to topic %s", topic);
    return 0;
}

void pubsub_publish(const char *topic, void *data, size_t size) {
    if (!topic || !data) {
        logger_log(LOG_ERROR, "Invalid topic or data");
        return;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    if (pthread_mutex_timedlock(&ps.mutex, &ts) != 0) return;
    struct subscriber *sub = ps.subscribers;
    while (sub) {
        if (strcmp(sub->topic, topic) == 0) {
            if (sub->callback) {
                sub->callback(data, size);
                event_pair_signal1(&ps.ep);
                event_pair_wait2(&ps.ep); // Two-way synchronization
            } else {
                logger_log(LOG_ERROR, "Invalid callback for topic %s", topic);
            }
        }
        sub = sub->next;
    }
    pthread_mutex_unlock(&ps.mutex);
    logger_log(LOG_DEBUG, "Published to topic %s", topic);
}