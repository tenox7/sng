#ifndef RINGBUF_H
#define RINGBUF_H

#include "compat.h"
#include "os/os_interface.h"

typedef struct {
    double *data;
    uint32_t size;
    atomic_uint_fast32_t head;
    atomic_uint_fast32_t tail;
    atomic_uint_fast32_t count;
    plot_mutex_t *write_mutex;
    plot_mutex_t *resize_mutex;
} ringbuf_t;

ringbuf_t *ringbuf_create(uint32_t size);
void ringbuf_destroy(ringbuf_t *ringbuf);
int ringbuf_resize(ringbuf_t *ringbuf, uint32_t new_size);
int ringbuf_push(ringbuf_t *ringbuf, double value);
int ringbuf_pop(ringbuf_t *ringbuf, double *value);
uint32_t ringbuf_count(ringbuf_t *ringbuf);
int ringbuf_is_full(ringbuf_t *ringbuf);
int ringbuf_is_empty(ringbuf_t *ringbuf);
int ringbuf_read_snapshot(ringbuf_t *ringbuf, double *buffer, uint32_t buffer_size, uint32_t *count_out, uint32_t *head_out, uint32_t *tail_out);

#endif
