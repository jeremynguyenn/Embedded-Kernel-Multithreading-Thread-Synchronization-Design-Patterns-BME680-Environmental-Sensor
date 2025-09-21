#ifndef RECURSIVE_MUTEX_H
#define RECURSIVE_MUTEX_H

typedef struct recursive_mutex recursive_mutex_t;

int recursive_mutex_init(recursive_mutex_t *rmutex);
void recursive_mutex_destroy(recursive_mutex_t *rmutex);
int recursive_mutex_lock(recursive_mutex_t *rmutex);
int recursive_mutex_unlock(recursive_mutex_t *rmutex);

#endif /* RECURSIVE_MUTEX_H */