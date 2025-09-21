#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include "thread_pool.h"
#include "logger.h"
#include "barrier.h"
#include "event_pair.h"

struct task {
    void (*func)(void *);
    void *arg;
    struct task *next;
};

struct thread_pool {
    pthread_t *threads;
    int num_threads;
    struct task *head;
    struct task *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    barrier_t *barrier;
    event_pair_t *ep;
    volatile int shutdown;
};

static void task_cleanup(void *arg) {
    struct task *task = (struct task *)arg;
    logger_log(LOG_INFO, "Task cleanup: freeing task");
    free(task);
}

static void *worker_thread(void *arg) {
    struct thread_pool *tp = (struct thread_pool *)arg;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(sched_getcpu() % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    while (!tp->shutdown) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += 5;
        pthread_mutex_lock(&tp->mutex);
        while (!tp->head && !tp->shutdown) {
            int ret = pthread_cond_timedwait(&tp->cond, &tp->mutex, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&tp->mutex);
                logger_log(LOG_WARNING, "Worker thread timed out waiting for tasks");
                continue;
            }
            if (ret != 0) {
                pthread_mutex_unlock(&tp->mutex);
                logger_log(LOG_ERROR, "Worker thread wait failed: %s", strerror(ret));
                return NULL;
            }
        }
        if (tp->shutdown) {
            pthread_mutex_unlock(&tp->mutex);
            break;
        }
        struct task *task = tp->head;
        if (task) {
            tp->head = task->next;
            if (!tp->head) tp->tail = NULL;
        }
        pthread_mutex_unlock(&tp->mutex);
        if (task) {
            pthread_cleanup_push(task_cleanup, task); // Cleanup if canceled
            barrier_wait(tp->barrier);
            if (task->func) task->func(task->arg);
            event_pair_signal1(tp->ep);
            event_pair_wait2(tp->ep);
            pthread_cleanup_pop(0);
            free(task);
        }
    }
    return NULL;
}

int thread_pool_init(struct thread_pool **tp, int num_threads) {
    *tp = malloc(sizeof(struct thread_pool));
    if (!*tp) return -ENOMEM;
    (*tp)->threads = malloc(num_threads * sizeof(pthread_t));
    if (!(*tp)->threads) {
        free(*tp);
        return -ENOMEM;
    }
    pthread_mutex_init(&(*tp)->mutex, NULL);
    pthread_cond_init(&(*tp)->cond, NULL);
    barrier_init(&(*tp)->barrier, num_threads + 1);
    event_pair_init(&(*tp)->ep);
    (*tp)->num_threads = num_threads;
    (*tp)->head = NULL;
    (*tp)->tail = NULL;
    (*tp)->shutdown = 0;
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&(*tp)->threads[i], NULL, worker_thread, *tp) != 0) {
            logger_log(LOG_ERROR, "Failed to create thread %d", i);
            thread_pool_destroy(*tp);
            return -1;
        }
    }
    logger_log(LOG_INFO, "Thread pool initialized with %d threads", num_threads);
    return 0;
}

void thread_pool_destroy(struct thread_pool *tp) {
    pthread_mutex_lock(&tp->mutex);
    tp->shutdown = 1;
    pthread_cond_broadcast(&tp->cond);
    pthread_mutex_unlock(&tp->mutex);
    for (int i = 0; i < tp->num_threads; i++) {
        pthread_join(tp->threads[i], NULL);
    }
    struct task *task = tp->head;
    while (task) {
        struct task *next = task->next;
        free(task);
        task = next;
    }
    pthread_mutex_destroy(&tp->mutex);
    pthread_cond_destroy(&tp->cond);
    barrier_destroy(tp->barrier);
    event_pair_destroy(tp->ep);
    free(tp->threads);
    free(tp);
    logger_log(LOG_INFO, "Thread pool destroyed");
}

int thread_pool_enqueue(struct thread_pool *tp, void (*func)(void *), void *arg) {
    if (!func) {
        logger_log(LOG_ERROR, "Invalid task function");
        return -EINVAL;
    }
    struct task *task = malloc(sizeof(struct task));
    if (!task) {
        logger_log(LOG_ERROR, "Failed to allocate task");
        return -ENOMEM;
    }
    task->func = func;
    task->arg = arg;
    task->next = NULL;
    pthread_mutex_lock(&tp->mutex);
    if (!tp->head) {
        tp->head = task;
        tp->tail = task;
    } else {
        tp->tail->next = task;
        tp->tail = task;
    }
    pthread_cond_signal(&tp->cond);
    pthread_mutex_unlock(&tp->mutex);
    logger_log(LOG_DEBUG, "Task enqueued");
    return 0;
}