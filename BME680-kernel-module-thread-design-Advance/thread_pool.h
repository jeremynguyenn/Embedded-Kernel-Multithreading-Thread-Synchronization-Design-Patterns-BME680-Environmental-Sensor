#ifndef THREAD_POOL_H
#define THREAD_POOL_H

struct thread_pool;

int thread_pool_init(struct thread_pool **tp, int num_threads);
void thread_pool_destroy(struct thread_pool *tp);
int thread_pool_enqueue(struct thread_pool *tp, void (*func)(void *), void *arg);

#endif /* THREAD_POOL_H */