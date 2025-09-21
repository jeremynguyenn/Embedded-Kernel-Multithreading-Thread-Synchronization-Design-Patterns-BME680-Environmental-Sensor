#ifndef ASSEMBLY_LINE_H
#define ASSEMBLY_LINE_H

#include "bme680.h"
#include "barrier.h"
#include "dining_philosophers.h"
#include "logger.h"

struct assembly_line;

int assembly_line_init(struct assembly_line **al, int num_stages);
void assembly_line_destroy(struct assembly_line *al);
int assembly_line_process(struct assembly_line *al, struct bme680_fifo_data *data);
int assembly_line_get_result(struct assembly_line *al, struct bme680_fifo_data *data);

#endif /* ASSEMBLY_LINE_H */