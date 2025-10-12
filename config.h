#ifndef CONFIG_H
#define CONFIG_H

#include "compat.h"
#include "graphics.h"

typedef struct {
    char *name;
    char *type;
    char *target;
    color_t line_color;
    color_t line_color_secondary; // For dual-line plots (OUT color)
    color_t background_color;
    int32_t height;
    int32_t refresh_interval_ms;
} plot_config_t;

typedef enum {
    FULLSCREEN_OFF = 0,
    FULLSCREEN_ON = 1,
    FULLSCREEN_FORCE = 2
} fullscreen_mode_t;

typedef struct {
    color_t background_color;
    color_t text_color;
    color_t border_color;
    color_t line_color;
    color_t line_color_secondary;
    color_t error_line_color;
    int32_t default_height;
    int32_t default_width;
    int32_t refresh_interval_ms;
    int32_t window_margin;
    int32_t max_fps;
    fullscreen_mode_t fullscreen;
    int fps_counter;
    float font_size;
    char *font_name;

    plot_config_t *plots;
    uint32_t plot_count;
} config_t;

config_t *config_load(const char *filename);
void config_destroy(config_t *config);
int config_get_max_fps(void);

#endif
