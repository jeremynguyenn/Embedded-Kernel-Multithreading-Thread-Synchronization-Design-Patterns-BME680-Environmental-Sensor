#ifndef FORK_HANDLER_H
#define FORK_HANDLER_H

typedef struct fork_handler fork_handler_t;

int fork_handler_init(fork_handler_t *fh, int num_threads);
void fork_handler_destroy(fork_handler_t *fh);
int fork_handler_fork(fork_handler_t *fh);

#endif /* FORK_HANDLER_H */