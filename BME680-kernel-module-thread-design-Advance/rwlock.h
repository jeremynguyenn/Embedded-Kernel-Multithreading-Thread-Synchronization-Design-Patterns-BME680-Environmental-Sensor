#ifndef RWLOCK_H
#define RWLOCK_H

typedef struct rwlock rwlock_t;

int rwlock_init(rwlock_t *rwlock);
void rwlock_destroy(rwlock_t *rwlock);
int rwlock_rdlock(rwlock_t *rwlock);
int rwlock_wrlock(rwlock_t *rwlock);
int rwlock_unlock(rwlock_t *rwlock);

#endif /* RWLOCK_H */