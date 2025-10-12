#ifndef PLOT_H
#define PLOT_H

#include "compat.h"
#include "config.h"
#include "ringbuf.h"
#include "graphics.h"
#include "threading.h"

typedef struct {
    plot_config_t *config;
    ringbuf_t *data_buffer;
    ringbuf_t *data_buffer_secondary; // For dual-line plots (OUT data)
    data_source_t *data_source; // Reference to data source for statistics
    int active;
    int is_dual; // True for dual-line plots like SNMP

    /* Statistics caching fields */
    uint32_t cached_data_count;
    uint32_t cached_data_count_secondary;
    uint32_t cached_head_position;
    uint32_t cached_head_position_secondary;
    int stats_dirty;
} plot_t;

typedef struct {
    config_t *config;
    plot_t *plots;
    uint32_t plot_count;
    window_t *window;
    renderer_t *renderer;
    font_t *font;
    int fullscreen;
    int32_t last_plot_width;

    /* Window size caching */
    int32_t cached_window_width;
    int32_t cached_window_height;
    int window_size_dirty;

    /* Rendering optimization */
    int needs_redraw;

    /* Fullscreen recheck timing */
    uint64_t last_fullscreen_check_ms;

    /* Mouse tracking */
    int32_t mouse_x;
    int32_t mouse_y;

} plot_system_t;

plot_system_t *plot_system_create(config_t *config);
void plot_system_destroy(plot_system_t *system);
int plot_system_update(plot_system_t *system);
void plot_system_connect_data_buffers(plot_system_t *system, data_collector_t *collector);

void plot_draw(plot_t *plot, renderer_t *renderer, font_t *font,
               int32_t x, int32_t y, int32_t width, int32_t height, config_t *global_config, uint32_t plot_index,
               int32_t hover_x, int32_t hover_y);

#endif
