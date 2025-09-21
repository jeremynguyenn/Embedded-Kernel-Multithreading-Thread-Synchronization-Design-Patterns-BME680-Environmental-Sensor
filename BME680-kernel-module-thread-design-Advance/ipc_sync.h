#ifndef IPC_SYNC_H
#define IPC_SYNC_H

#include "bme680.h"

typedef struct ipc_sync ipc_sync_t;

int ipc_sync_init(ipc_sync_t *is, key_t key);
void ipc_sync_destroy(ipc_sync_t *is);
int ipc_sync_write(ipc_sync_t *is, struct bme680_fifo_data *data);
int ipc_sync_read(ipc_sync_t *is, struct bme680_fifo_data *data);

#endif /* IPC_SYNC_H */