#ifndef TIMER_H
#define TIMER_H

typedef struct timer timer_t;

int timer_init(timer_t *timer, long interval_ms, void (*callback)(void *), void *arg);
void timer_destroy(timer_t *timer);

#endif /* TIMER_H */