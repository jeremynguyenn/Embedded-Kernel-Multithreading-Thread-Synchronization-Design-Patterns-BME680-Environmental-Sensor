#ifndef EVENT_PAIR_H
#define EVENT_PAIR_H

typedef struct event_pair event_pair_t;

int event_pair_init(event_pair_t *ep);
void event_pair_destroy(event_pair_t *ep);
int event_pair_signal1(event_pair_t *ep);
int event_pair_wait1(event_pair_t *ep);
int event_pair_signal2(event_pair_t *ep);
int event_pair_wait2(event_pair_t *ep);

#endif /* EVENT_PAIR_H */