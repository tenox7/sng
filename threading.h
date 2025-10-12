#ifndef THREADING_H
#define THREADING_H

#include "compat.h"
#include "os/os_interface.h"
#include "ringbuf.h"
#include "config.h"
#include "datasource.h"

typedef struct {
    char *type;
    char *target;
    datasource_t *datasource;
    ringbuf_t *data_buffer;
    ringbuf_t *data_buffer_secondary;
    plot_thread_t *thread;
    int32_t refresh_interval_ms;
    int is_dual;
} data_source_t;

typedef struct {
    data_source_t *sources;
    uint32_t source_count;
} data_collector_t;

data_collector_t *data_collector_create(config_t *config);
void data_collector_destroy(data_collector_t *collector);
int data_collector_start(data_collector_t *collector);

#endif
