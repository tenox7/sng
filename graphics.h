#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "compat.h"

typedef struct {
    uint8_t r, g, b, a;
} color_t;

typedef struct {
    int32_t x, y, w, h;
} rect_t;

typedef struct {
    void *handle;
} font_t;

typedef struct {
    void *handle;
} window_t;

typedef struct {
    void *handle;
} renderer_t;

int graphics_init(void);
void graphics_cleanup(void);

window_t *window_create(const char *title, int32_t width, int32_t height);
void window_destroy(window_t *window);
void window_set_fullscreen(window_t *window, int fullscreen);
int window_is_fullscreen(window_t *window);
void window_set_topmost(window_t *window, int topmost);
void window_get_size(window_t *window, int32_t *width, int32_t *height);
int window_was_resized(void);

renderer_t *renderer_create(window_t *window);
void renderer_destroy(renderer_t *renderer);
void renderer_clear(renderer_t *renderer, color_t color);
void renderer_present(renderer_t *renderer);
void renderer_set_color(renderer_t *renderer, color_t color);
void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void renderer_draw_rect(renderer_t *renderer, rect_t rect);
void renderer_fill_rect(renderer_t *renderer, rect_t rect);

font_t *font_create(const char *path, int32_t size);
void font_destroy(font_t *font);
void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text);
void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height);

typedef enum {
    KEY_Q = 'q',
    KEY_R = 'r',
    KEY_F = 'f'
} key_code_t;

typedef enum {
    GRAPHICS_EVENT_NONE,
    GRAPHICS_EVENT_QUIT,
    GRAPHICS_EVENT_KEY_PRESS,
    GRAPHICS_EVENT_REFRESH,
    GRAPHICS_EVENT_FULLSCREEN_TOGGLE,
    GRAPHICS_EVENT_MOUSE_MOTION
} graphics_event_type_t;

typedef struct {
    graphics_event_type_t type;
    key_code_t key;
    int32_t mouse_x;
    int32_t mouse_y;
} graphics_event_t;

int graphics_poll_events(void);
int graphics_wait_events(void);
int graphics_get_event(graphics_event_t *event);
void graphics_start_render_timer(int fps);
void graphics_stop_render_timer(void);
void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, int enabled);
void graphics_get_mouse_position(int32_t *x, int32_t *y);

#endif
