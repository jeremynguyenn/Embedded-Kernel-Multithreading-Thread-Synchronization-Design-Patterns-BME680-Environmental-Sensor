#ifndef DINING_PHILOSOPHERS_H
#define DINING_PHILOSOPHERS_H

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "logger.h"

typedef struct dining_philosophers dining_philosophers_t;

struct dining_philosophers {
    int num_philosophers;
    pthread_mutex_t *forks;
    volatile int running;
};

int dining_philosophers_init(dining_philosophers_t **dp, int num_philosophers);
void dining_philosophers_destroy(dining_philosophers_t *dp);
int dining_philosophers_think(dining_philosophers_t *dp, int id);
int dining_philosophers_eat(dining_philosophers_t *dp, int id);
int dining_philosophers_done(dining_philosophers_t *dp, int id);

#endif /* DINING_PHILOSOPHERS_H */