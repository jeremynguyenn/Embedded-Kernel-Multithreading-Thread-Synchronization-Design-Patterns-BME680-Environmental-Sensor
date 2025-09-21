#ifndef MONITOR_H
#define MONITOR_H

#include "bme680.h"
#include "rwlock.h"
#include "deadlock_detector.h"

struct bme680_monitor;

int bme680_monitor_init(struct bme680_monitor **monitor, int size);
void bme680_monitor_destroy(struct bme680_monitor *monitor);
int bme680_monitor_write(struct bme680_monitor *monitor, struct bme680_fifo_data *data);
int bme680_monitor_read(struct bme680_monitor *monitor, struct bme680_fifo_data *data);

#endif /* MONITOR_H */