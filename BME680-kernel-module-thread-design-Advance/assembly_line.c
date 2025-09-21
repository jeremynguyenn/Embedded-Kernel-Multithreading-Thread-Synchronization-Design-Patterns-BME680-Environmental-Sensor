#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "assembly_line.h"
#include "logger.h"
#include "barrier.h"
#include "dining_philosophers.h"

struct stage {
    pthread_t thread;
    volatile int running;
    int id;
    struct assembly_line *al;
};

struct assembly_line {
    struct stage *stages;
    int num_stages;
    barrier_t *barrier;
    dining_philosophers_t *dp;
    struct bme680_fifo_data *data;
    pthread_mutex_t mutex;
};

static void stage_cleanup(void *arg) {
    struct stage *s = (struct stage *)arg;
    logger_log(LOG_INFO, "Stage cleanup: freeing stage %d", s->id);
    free(s);
}

static void *stage_thread(void *arg) {
    struct stage *s = (struct stage *)arg;
    pthread_cleanup_push(stage_cleanup, s);
    while (s->running) {
        barrier_wait(s->al->barrier);
        dining_philosophers_think(s->al->dp, s->id);
        // Process data
        s->al->data->temp += 1.0; // Simulate
        dining_philosophers_eat(s->al->dp, s->id);
        dining_philosophers_done(s->al->dp, s->id);
    }
    pthread_cleanup_pop(0);
    return NULL;
}

int assembly_line_init(struct assembly_line **al, int num_stages) {
    *al = malloc(sizeof(struct assembly_line));
    if (!*al) return -ENOMEM;
    (*al)->stages = malloc(num_stages * sizeof(struct stage));
    if (!(*al)->stages) {
        free(*al);
        return -ENOMEM;
    }
    (*al)->num_stages = num_stages;
    pthread_mutex_init(&(*al)->mutex, NULL);
    barrier_init(&(*al)->barrier, num_stages);
    dining_philosophers_init(&(*al)->dp, num_stages);
    (*al)->data = malloc(sizeof(struct bme680_fifo_data));
    if (!(*al)->data) {
        assembly_line_destroy(*al);
        return -ENOMEM;
    }
    for (int i = 0; i < num_stages; i++) {
        (*al)->stages[i].id = i;
        (*al)->stages[i].al = *al;
        (*al)->stages[i].running = 1;
        if (pthread_create(&(*al)->stages[i].thread, NULL, stage_thread, &(*al)->stages[i]) != 0) {
            logger_log(LOG_ERROR, "Failed to create stage thread %d", i);
            assembly_line_destroy(*al);
            return -1;
        }
    }
    logger_log(LOG_INFO, "Assembly line initialized with %d stages", num_stages);
    return 0;
}

void assembly_line_destroy(struct assembly_line *al) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5;
    if (pthread_mutex_timedlock(&al->mutex, &ts) != 0) {
        logger_log(LOG_ERROR, "Timed out locking for destroy");
        return;
    }
    for (int i = 0; i < al->num_stages; i++) {
        al->stages[i].running = 0;
        pthread_cancel(al->stages[i].thread);
        pthread_join(al->stages[i].thread, NULL);
    }
    free(al->data);
    barrier_destroy(al->barrier);
    dining_philosophers_destroy(al->dp);
    pthread_mutex_unlock(&al->mutex);
    pthread_mutex_destroy(&al->mutex);
    free(al->stages);
    free(al);
    logger_log(LOG_INFO, "Assembly line destroyed");
}

int assembly_line_process(struct assembly_line *al, struct bme680_fifo_data *data) {
    pthread_mutex_lock(&al->mutex);
    memcpy(al->data, data, sizeof(struct bme680_fifo_data));
    pthread_mutex_unlock(&al->mutex);
    return 0;
}

int assembly_line_get_result(struct assembly_line *al, struct bme680_fifo_data *result) {
    pthread_mutex_lock(&al->mutex);
    memcpy(result, al->data, sizeof(struct bme680_fifo_data));
    pthread_mutex_unlock(&al->mutex);
    return 0;
}