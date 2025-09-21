#ifndef BARRIER_H
#define BARRIER_H

typedef struct barrier barrier_t;

int barrier_init(barrier_t **barrier, int total);
void barrier_destroy(barrier_t *barrier);
int barrier_wait(barrier_t *barrier);

#endif /* BARRIER_H */