#ifndef PUBSUB_H
#define PUBSUB_H

void pubsub_init(void);
void pubsub_destroy(void);
int pubsub_subscribe(const char *topic, void (*callback)(void *, size_t));
void pubsub_publish(const char *topic, void *data, size_t size);

#endif /* PUBSUB_H */