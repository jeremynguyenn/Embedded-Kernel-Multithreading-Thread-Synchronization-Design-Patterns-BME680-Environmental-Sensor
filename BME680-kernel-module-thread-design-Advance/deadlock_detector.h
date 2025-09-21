#ifndef DEADLOCK_DETECTOR_H
#define DEADLOCK_DETECTOR_H

typedef struct deadlock_detector deadlock_detector_t;

int deadlock_detector_init(deadlock_detector_t *dd, int num_mutexes);
void deadlock_detector_destroy(deadlock_detector_t *dd);
int deadlock_detector_lock(deadlock_detector_t *dd, int mutex_id);
int deadlock_detector_unlock(deadlock_detector_t *dd, int mutex_id);

#endif /* DEADLOCK_DETECTOR_H */