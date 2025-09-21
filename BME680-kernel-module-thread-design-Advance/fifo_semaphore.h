#ifndef FIFO_SEMAPHORE_H
#define FIFO_SEMAPHORE_H

typedef struct fifo_semaphore fifo_semaphore_t;

int fifo_semaphore_init(fifo_semaphore_t *sem, int value);
void fifo_semaphore_destroy(fifo_semaphore_t *sem);
int fifo_semaphore_wait(fifo_semaphore_t *sem);
int fifo_semaphore_post(fifo_semaphore_t *sem);

#endif /* FIFO_SEMAPHORE_H */