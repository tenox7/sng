#include "../graphics.h"
#include <gtk/gtk.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    cairo_t *cr;
    int width, height;
    gboolean should_quit;
    GMainLoop *main_loop;
} gtk_window_context_t;

typedef struct {
    cairo_t *cr;
    gtk_window_context_t *window_context;
    color_t current_color;
} gtk_renderer_context_t;

typedef struct {
    PangoFontDescription *font_desc;
    PangoLayout *layout;
} gtk_font_context_t;

static int gtk_initialized = 0;
static gtk_window_context_t *gtk_active_window = NULL;

static graphics_event_t pending_event = {GRAPHICS_EVENT_NONE, 0, 0, 0};
static int fullscreen_state = 0;
static guint gtk_render_timer_id = 0;
static int window_resized = 0;
static int32_t current_mouse_x = 0;
static int32_t current_mouse_y = 0;
static uint64_t last_mouse_motion_time = 0;

static uint32_t frame_count = 0;
static uint64_t fps_last_time = 0;
static float current_fps = 0.0f;

static gboolean gtk_draw_callback(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean gtk_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
static void gtk_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data);
static gboolean gtk_motion_notify_callback(GtkWidget *widget, GdkEventMotion *event, gpointer data);

static uint64_t gtk_get_time_ms(void) {
    return g_get_monotonic_time() / 1000;
}

static gboolean gtk_render_timer_callback(gpointer user_data) {
    (void)user_data;

    if (gtk_active_window && gtk_active_window->drawing_area) {
        gtk_widget_queue_draw(gtk_active_window->drawing_area);
    }

    return G_SOURCE_CONTINUE;
}

static gboolean gtk_key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    (void)user_data;

    guint keyval = gdk_keyval_to_lower(event->keyval);

    switch (keyval) {
        case GDK_KEY_q:
            pending_event.type = GRAPHICS_EVENT_QUIT;
            pending_event.key = KEY_Q;
            break;
        case GDK_KEY_r:
            pending_event.type = GRAPHICS_EVENT_REFRESH;
            pending_event.key = KEY_R;
            break;
        case GDK_KEY_f:
            pending_event.type = GRAPHICS_EVENT_FULLSCREEN_TOGGLE;
            pending_event.key = KEY_F;
            break;
    }

    return TRUE;
}

int graphics_init(void) {
    if (gtk_initialized) {
        return 1;
    }

    gtk_init(NULL, NULL);

    fps_last_time = gtk_get_time_ms();
    frame_count = 0;
    current_fps = 0.0f;

    gtk_initialized = 1;
    return 1;
}

void graphics_cleanup(void) {
    if (!gtk_initialized) return;
    gtk_initialized = 0;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    window_t *window = malloc(sizeof(window_t));
    if (!window) return NULL;

    gtk_window_context_t *ctx = malloc(sizeof(gtk_window_context_t));
    if (!ctx) {
        free(window);
        return NULL;
    }

    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    ctx->drawing_area = gtk_drawing_area_new();
    ctx->surface = NULL;
    ctx->cr = NULL;
    ctx->width = width;
    ctx->height = height;
    ctx->should_quit = FALSE;
    ctx->main_loop = g_main_loop_new(NULL, FALSE);

    gtk_window_set_title(GTK_WINDOW(ctx->window), title);
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), width, height);
    gtk_window_set_resizable(GTK_WINDOW(ctx->window), TRUE);

    gtk_container_add(GTK_CONTAINER(ctx->window), ctx->drawing_area);

    ctx->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    ctx->cr = cairo_create(ctx->surface);

    cairo_set_source_rgb(ctx->cr, 1.0, 1.0, 1.0);
    cairo_paint(ctx->cr);

    g_signal_connect(ctx->drawing_area, "draw", G_CALLBACK(gtk_draw_callback), ctx);
    g_signal_connect(ctx->window, "delete-event", G_CALLBACK(gtk_delete_event), ctx);
    g_signal_connect(ctx->drawing_area, "size-allocate", G_CALLBACK(gtk_size_allocate), ctx);
    g_signal_connect(ctx->window, "key-press-event", G_CALLBACK(gtk_key_press_callback), ctx);
    g_signal_connect(ctx->drawing_area, "motion-notify-event", G_CALLBACK(gtk_motion_notify_callback), ctx);

    gtk_widget_add_events(ctx->drawing_area, GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK);

    gtk_widget_set_can_focus(ctx->window, TRUE);
    gtk_widget_grab_focus(ctx->window);

    gtk_widget_show_all(ctx->window);

    gtk_active_window = ctx;

    window->handle = ctx;
    return window;
}

void window_destroy(window_t *window) {
    if (!window) return;

    gtk_window_context_t *ctx = (gtk_window_context_t*)window->handle;
    if (ctx) {
        if (gtk_active_window == ctx) {
            gtk_active_window = NULL;
        }

        if (ctx->main_loop && g_main_loop_is_running(ctx->main_loop)) {
            g_main_loop_quit(ctx->main_loop);
        }
        if (ctx->main_loop) {
            g_main_loop_unref(ctx->main_loop);
        }
        if (ctx->surface) {
            cairo_surface_destroy(ctx->surface);
        }
        if (ctx->cr) {
            cairo_destroy(ctx->cr);
        }
        gtk_widget_destroy(ctx->window);
        free(ctx);
    }
    free(window);
}

void window_set_fullscreen(window_t *window, int fullscreen) {
    if (!window) return;

    gtk_window_context_t *ctx = (gtk_window_context_t*)window->handle;
    if (fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(ctx->window));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(ctx->window));
    }
    fullscreen_state = fullscreen;
}

int window_is_fullscreen(window_t *window) {
    if (!window) return 0;

    gtk_window_context_t *ctx = (gtk_window_context_t*)window->handle;
    GdkWindow *gdk_window = gtk_widget_get_window(ctx->window);
    if (!gdk_window) return 0;

    GdkWindowState state = gdk_window_get_state(gdk_window);
    return (state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
}

void window_set_topmost(window_t *window, int topmost) {
    if (!window) return;

    gtk_window_context_t *ctx = (gtk_window_context_t*)window->handle;
    gtk_window_set_keep_above(GTK_WINDOW(ctx->window), topmost);
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    if (!window || !width || !height) return;

    gtk_window_context_t *ctx = (gtk_window_context_t*)window->handle;
    *width = ctx->width;
    *height = ctx->height;
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *renderer = malloc(sizeof(renderer_t));
    if (!renderer) return NULL;

    gtk_renderer_context_t *ctx = malloc(sizeof(gtk_renderer_context_t));
    if (!ctx) {
        free(renderer);
        return NULL;
    }

    ctx->cr = NULL;
    ctx->window_context = (gtk_window_context_t*)window->handle;
    ctx->current_color = (color_t){255, 255, 255, 255};

    renderer->handle = ctx;
    return renderer;
}

void renderer_destroy(renderer_t *renderer) {
    if (!renderer) return;

    gtk_renderer_context_t *ctx = (gtk_renderer_context_t*)renderer->handle;
    if (ctx) {
        free(ctx);
    }
    free(renderer);
}

void renderer_clear(renderer_t *renderer, color_t color) {
    if (!renderer) return;

    gtk_renderer_context_t *ctx = (gtk_renderer_context_t*)renderer->handle;
    if (ctx->window_context->cr) {
        cairo_set_source_rgba(ctx->window_context->cr,
                             color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
        cairo_paint(ctx->window_context->cr);
    }
}

void renderer_present(renderer_t *renderer) {
    if (!renderer) return;
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    if (!renderer) return;

    gtk_renderer_context_t *ctx = (gtk_renderer_context_t*)renderer->handle;
    ctx->current_color = color;
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (!renderer) return;

    gtk_renderer_context_t *ctx = (gtk_renderer_context_t*)renderer->handle;
    if (ctx->window_context->cr) {
        cairo_set_source_rgba(ctx->window_context->cr,
                             ctx->current_color.r / 255.0, ctx->current_color.g / 255.0,
                             ctx->current_color.b / 255.0, ctx->current_color.a / 255.0);
        cairo_set_line_width(ctx->window_context->cr, 1.0);
        cairo_set_antialias(ctx->window_context->cr, CAIRO_ANTIALIAS_NONE);
        cairo_move_to(ctx->window_context->cr, x1 + 0.5, y1 + 0.5);
        cairo_line_to(ctx->window_context->cr, x2 + 0.5, y2 + 0.5);
        cairo_stroke(ctx->window_context->cr);
    }
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;

    gtk_renderer_context_t *ctx = (gtk_renderer_context_t*)renderer->handle;
    if (ctx->window_context->cr) {
        cairo_set_source_rgba(ctx->window_context->cr,
                             ctx->current_color.r / 255.0, ctx->current_color.g / 255.0,
                             ctx->current_color.b / 255.0, ctx->current_color.a / 255.0);
        cairo_set_line_width(ctx->window_context->cr, 1.0);
        cairo_set_antialias(ctx->window_context->cr, CAIRO_ANTIALIAS_NONE);
        cairo_rectangle(ctx->window_context->cr, rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1);
        cairo_stroke(ctx->window_context->cr);
    }
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;

    gtk_renderer_context_t *ctx = (gtk_renderer_context_t*)renderer->handle;
    if (ctx->window_context->cr) {
        cairo_set_source_rgba(ctx->window_context->cr,
                             ctx->current_color.r / 255.0, ctx->current_color.g / 255.0,
                             ctx->current_color.b / 255.0, ctx->current_color.a / 255.0);
        cairo_rectangle(ctx->window_context->cr, rect.x, rect.y, rect.w, rect.h);
        cairo_fill(ctx->window_context->cr);
    }
}

font_t *font_create(const char *path, int32_t size) {
    font_t *font = malloc(sizeof(font_t));
    if (!font) return NULL;

    gtk_font_context_t *ctx = malloc(sizeof(gtk_font_context_t));
    if (!ctx) {
        free(font);
        return NULL;
    }

    ctx->font_desc = pango_font_description_new();
    if (path && *path) {
        pango_font_description_set_family(ctx->font_desc, "monospace");
    } else {
        pango_font_description_set_family(ctx->font_desc, "Sans");
    }
    pango_font_description_set_size(ctx->font_desc, (size * PANGO_SCALE * 3) / 4);
    ctx->layout = NULL;

    font->handle = ctx;
    return font;
}

void font_destroy(font_t *font) {
    if (!font) return;

    gtk_font_context_t *ctx = (gtk_font_context_t*)font->handle;
    if (ctx) {
        if (ctx->layout) {
            g_object_unref(ctx->layout);
        }
        if (ctx->font_desc) {
            pango_font_description_free(ctx->font_desc);
        }
        free(ctx);
    }
    free(font);
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    if (!renderer || !font || !text) return;

    gtk_renderer_context_t *rctx = (gtk_renderer_context_t*)renderer->handle;
    gtk_font_context_t *fctx = (gtk_font_context_t*)font->handle;

    if (rctx->window_context->cr) {
        if (!fctx->layout) {
            fctx->layout = pango_cairo_create_layout(rctx->window_context->cr);
            pango_layout_set_font_description(fctx->layout, fctx->font_desc);
        }

        pango_layout_set_text(fctx->layout, text, -1);
        cairo_set_source_rgba(rctx->window_context->cr,
                             color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
        cairo_move_to(rctx->window_context->cr, x, y);
        pango_cairo_show_layout(rctx->window_context->cr, fctx->layout);
    }
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    if (!font || !text) return;

    gtk_font_context_t *ctx = (gtk_font_context_t*)font->handle;
    if (ctx->layout) {
        pango_layout_set_text(ctx->layout, text, -1);
        int w, h;
        pango_layout_get_pixel_size(ctx->layout, &w, &h);
        if (width) *width = w;
        if (height) *height = h;
    }
}

int graphics_poll_events(void) {
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    if (gtk_active_window && gtk_active_window->should_quit) {
        return 0;
    }

    return 1;
}

static uint64_t gtk_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int graphics_wait_events(void) {
    static uint64_t last_frame_time = 0;
    uint64_t current_time_ms;
    uint64_t time_since_mouse_motion;
    uint64_t frame_interval_us;
    uint64_t current_time;

    extern int config_get_max_fps(void);
    int fps = config_get_max_fps();
    if (fps <= 0) fps = 1;

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    if (gtk_active_window && gtk_active_window->should_quit) {
        return 0;
    }

    current_time_ms = gtk_get_time_ms();
    time_since_mouse_motion = current_time_ms - last_mouse_motion_time;

    if (time_since_mouse_motion < 1000) {
        frame_interval_us = 8000;
    } else {
        frame_interval_us = 1000000 / fps;
    }

    current_time = gtk_get_time_us();

    if (last_frame_time != 0) {
        uint64_t elapsed_us = current_time - last_frame_time;
        if (elapsed_us < frame_interval_us) {
            uint64_t sleep_us = frame_interval_us - elapsed_us;
            usleep(sleep_us);
        }
    }

    last_frame_time = gtk_get_time_us();
    return 1;
}

int graphics_get_event(graphics_event_t *event) {
    if (!event) return 0;

    if (pending_event.type != GRAPHICS_EVENT_NONE) {
        *event = pending_event;
        pending_event.type = GRAPHICS_EVENT_NONE;
        return 1;
    }

    event->type = GRAPHICS_EVENT_NONE;
    return 0;
}

void graphics_start_render_timer(int fps) {
    if (gtk_render_timer_id != 0) {
        g_source_remove(gtk_render_timer_id);
    }

    guint interval = 1000 / fps;
    gtk_render_timer_id = g_timeout_add(interval, gtk_render_timer_callback, NULL);
}

void graphics_stop_render_timer(void) {
    if (gtk_render_timer_id != 0) {
        g_source_remove(gtk_render_timer_id);
        gtk_render_timer_id = 0;
    }
}

static gboolean gtk_draw_callback(GtkWidget *widget, cairo_t *cr, gpointer data) {
    gtk_window_context_t *ctx = (gtk_window_context_t*)data;

    if (ctx->surface) {
        cairo_set_source_surface(cr, ctx->surface, 0, 0);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    return TRUE;
}

static gboolean gtk_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    gtk_window_context_t *ctx = (gtk_window_context_t*)data;
    ctx->should_quit = TRUE;

    return FALSE;
}

static void gtk_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
    gtk_window_context_t *ctx = (gtk_window_context_t*)data;

    if (ctx->width != allocation->width || ctx->height != allocation->height) {
        ctx->width = allocation->width;
        ctx->height = allocation->height;
        window_resized = 1;

        if (ctx->surface) {
            cairo_surface_destroy(ctx->surface);
        }
        if (ctx->cr) {
            cairo_destroy(ctx->cr);
        }

        ctx->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                 ctx->width, ctx->height);
        ctx->cr = cairo_create(ctx->surface);

        cairo_set_source_rgb(ctx->cr, 1.0, 1.0, 1.0);
        cairo_paint(ctx->cr);

        gtk_widget_queue_draw(widget);
    }
}

static gboolean gtk_motion_notify_callback(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    gtk_window_context_t *ctx;

    ctx = (gtk_window_context_t*)data;

    current_mouse_x = (int32_t)event->x;
    current_mouse_y = (int32_t)event->y;
    last_mouse_motion_time = gtk_get_time_ms();
    pending_event.type = GRAPHICS_EVENT_MOUSE_MOTION;
    pending_event.mouse_x = current_mouse_x;
    pending_event.mouse_y = current_mouse_y;

    gtk_widget_queue_draw(widget);

    return TRUE;
}

int window_was_resized(void) {
    int result = window_resized;
    window_resized = 0;
    return result;
}

void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, int enabled) {
    if (!enabled || !renderer || !font) return;

    frame_count++;
    uint64_t current_time = gtk_get_time_ms();

    if (current_time - fps_last_time >= 1000) {
        current_fps = (float)frame_count * 1000.0f / (float)(current_time - fps_last_time);
        frame_count = 0;
        fps_last_time = current_time;
    }

    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", current_fps);

    color_t fps_bg_color = {0, 0, 0, 180};
    color_t fps_text_color = {255, 255, 0, 255};
    rect_t fps_bg_rect = {5, 5, 80, 20};

    renderer_set_color(renderer, fps_bg_color);
    renderer_fill_rect(renderer, fps_bg_rect);

    font_draw_text(renderer, font, fps_text_color, 10, 8, fps_text);
}

void graphics_get_mouse_position(int32_t *x, int32_t *y) {
    if (x) *x = current_mouse_x;
    if (y) *y = current_mouse_y;
}