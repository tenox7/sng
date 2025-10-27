#include "compat.h"
#include "ringbuf.h"
#include "os/os_interface.h"
#include <stdlib.h>
#include <string.h>

ringbuf_t *ringbuf_create(uint32_t size) {
    ringbuf_t *ringbuf;

    if (size == 0) return NULL;

    ringbuf = malloc(sizeof(ringbuf_t));
    if (!ringbuf) return NULL;

    ringbuf->data = malloc(sizeof(double) * size);
    if (!ringbuf->data) {
        free(ringbuf);
        return NULL;
    }

    ringbuf->size = size;
    atomic_store(&ringbuf->head, 0);
    atomic_store(&ringbuf->tail, 0);
    atomic_store(&ringbuf->count, 0);

    ringbuf->write_mutex = os_plot_mutex_create();
    if (!ringbuf->write_mutex) {
        free(ringbuf->data);
        free(ringbuf);
        return NULL;
    }

    ringbuf->resize_mutex = os_plot_mutex_create();
    if (!ringbuf->resize_mutex) {
        os_plot_mutex_destroy(ringbuf->write_mutex);
        free(ringbuf->data);
        free(ringbuf);
        return NULL;
    }

    memset(ringbuf->data, 0, sizeof(double) * size);

    return ringbuf;
}

void ringbuf_destroy(ringbuf_t *ringbuf) {
    if (!ringbuf) return;

    os_plot_mutex_destroy(ringbuf->write_mutex);
    os_plot_mutex_destroy(ringbuf->resize_mutex);
    free(ringbuf->data);
    free(ringbuf);
}

int ringbuf_resize(ringbuf_t *ringbuf, uint32_t new_size) {
    double *new_data;
    uint32_t current_count;
    uint32_t current_head;
    uint32_t current_tail;
    uint32_t copy_count;
    uint32_t i;
    uint32_t src_index;

    if (!ringbuf || new_size == 0) return 0;

    os_plot_mutex_lock(ringbuf->resize_mutex);
    os_plot_mutex_lock(ringbuf->write_mutex);

    if (new_size == ringbuf->size) {
        os_plot_mutex_unlock(ringbuf->write_mutex);
        os_plot_mutex_unlock(ringbuf->resize_mutex);
        return 1;
    }

    new_data = malloc(sizeof(double) * new_size);
    if (!new_data) {
        os_plot_mutex_unlock(ringbuf->write_mutex);
        os_plot_mutex_unlock(ringbuf->resize_mutex);
        return 0;
    }

    current_count = atomic_load(&ringbuf->count);
    current_head = atomic_load(&ringbuf->head);
    current_tail = atomic_load(&ringbuf->tail);
    copy_count = (current_count < new_size) ? current_count : new_size;

    if (copy_count > 0) {
        for (i = 0; i < copy_count; i++) {
            if (new_size < current_count) {
                src_index = (current_head - copy_count + i + ringbuf->size) % ringbuf->size;
            } else {
                src_index = (current_tail + i) % ringbuf->size;
            }
            new_data[i] = ringbuf->data[src_index];
        }
    }

    free(ringbuf->data);
    ringbuf->data = new_data;
    ringbuf->size = new_size;
    atomic_store(&ringbuf->head, copy_count % new_size);
    atomic_store(&ringbuf->tail, 0);
    atomic_store(&ringbuf->count, copy_count);

    memset(&ringbuf->data[copy_count], 0, sizeof(double) * (new_size - copy_count));

    os_plot_mutex_unlock(ringbuf->write_mutex);
    os_plot_mutex_unlock(ringbuf->resize_mutex);
    return 1;
}

int ringbuf_push(ringbuf_t *ringbuf, double value) {
    uint32_t current_head;
    uint32_t current_count;
    uint32_t new_head;

    if (!ringbuf) return 0;

    os_plot_mutex_lock(ringbuf->write_mutex);

    current_head = atomic_load(&ringbuf->head);
    current_count = atomic_load(&ringbuf->count);

    ringbuf->data[current_head] = value;
    new_head = (current_head + 1) % ringbuf->size;
    atomic_store(&ringbuf->head, new_head);

    if (current_count < ringbuf->size) {
        atomic_store(&ringbuf->count, current_count + 1);
    } else {
        uint32_t current_tail = atomic_load(&ringbuf->tail);
        atomic_store(&ringbuf->tail, (current_tail + 1) % ringbuf->size);
    }

    os_plot_mutex_unlock(ringbuf->write_mutex);
    return 1;
}

int ringbuf_pop(ringbuf_t *ringbuf, double *value) {
    uint32_t current_count;
    uint32_t current_tail;
    uint32_t new_tail;

    if (!ringbuf || !value) return 0;

    os_plot_mutex_lock(ringbuf->write_mutex);

    current_count = atomic_load(&ringbuf->count);
    if (current_count == 0) {
        os_plot_mutex_unlock(ringbuf->write_mutex);
        return 0;
    }

    current_tail = atomic_load(&ringbuf->tail);
    *value = ringbuf->data[current_tail];
    new_tail = (current_tail + 1) % ringbuf->size;
    atomic_store(&ringbuf->tail, new_tail);
    atomic_store(&ringbuf->count, current_count - 1);

    os_plot_mutex_unlock(ringbuf->write_mutex);
    return 1;
}

uint32_t ringbuf_count(ringbuf_t *ringbuf) {
    if (!ringbuf) return 0;

    return atomic_load(&ringbuf->count);
}

int ringbuf_is_full(ringbuf_t *ringbuf) {
    if (!ringbuf) return 0;

    return (atomic_load(&ringbuf->count) == ringbuf->size);
}

int ringbuf_is_empty(ringbuf_t *ringbuf) {
    if (!ringbuf) return 1;

    return (atomic_load(&ringbuf->count) == 0);
}

int ringbuf_read_snapshot(ringbuf_t *ringbuf, double *buffer, uint32_t buffer_size, uint32_t *count_out, uint32_t *head_out, uint32_t *tail_out) {
    uint32_t count, head, tail;
    uint32_t attempts;
    const uint32_t max_attempts = 10;
    uint32_t copy_count;
    uint32_t i;
    uint32_t idx;
    uint32_t verify_count;
    uint32_t verify_head;
    uint32_t verify_tail;

    if (!ringbuf || !buffer || !count_out || !head_out || !tail_out) return 0;

    attempts = 0;

    do {
        count = atomic_load(&ringbuf->count);
        head = atomic_load(&ringbuf->head);
        tail = atomic_load(&ringbuf->tail);

        if (count == 0) {
            *count_out = 0;
            return 1;
        }

        copy_count = (count < buffer_size) ? count : buffer_size;

        for (i = 0; i < copy_count; i++) {
            idx = (tail + i) % ringbuf->size;
            buffer[i] = ringbuf->data[idx];
        }

        verify_count = atomic_load(&ringbuf->count);
        verify_head = atomic_load(&ringbuf->head);
        verify_tail = atomic_load(&ringbuf->tail);

        if (count == verify_count && head == verify_head && tail == verify_tail) {
            *count_out = copy_count;
            *head_out = head;
            *tail_out = tail;
            return 1;
        }

        attempts++;
    } while (attempts < max_attempts);

    return 0;
}
