#define _GNU_SOURCE
#include "../graphics.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

typedef struct {
    uint8_t r, g, b, a;
    unsigned long pixel;
} color_cache_entry_t;

#define COLOR_CACHE_SIZE 256

typedef struct {
    Display *display;
    Window window;
    Pixmap pixmap;
    int screen;
    GC gc;
    Colormap colormap;
    XFontStruct *font_info;
    int width, height;
    int should_quit;
    unsigned long bg_color;
    Atom wm_delete_window;
    color_cache_entry_t color_cache[COLOR_CACHE_SIZE];
    int color_cache_count;
} x11_window_context_t;

typedef struct {
    x11_window_context_t *window_context;
    unsigned long current_color;
    unsigned long last_set_color;
} x11_renderer_context_t;

typedef struct {
    XFontStruct *font_struct;
    Display *display;
} x11_font_context_t;

static int x11_initialized = 0;
static x11_window_context_t *x11_active_window = NULL;
static int window_resized = 0;

static graphics_event_t pending_event = {GRAPHICS_EVENT_NONE, 0, 0, 0};
static int fullscreen_state = 0;
static int32_t current_mouse_x = 0;
static int32_t current_mouse_y = 0;
static uint64_t last_mouse_motion_time = 0;

static uint32_t frame_count = 0;
static uint64_t fps_last_time = 0;
static float current_fps = 0.0f;

static uint64_t x11_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static unsigned long x11_create_color_cached(x11_window_context_t *ctx, color_t color) {
    Colormap colormap;
    XColor xcolor;
    int brightness;
    int i;

    for (i = 0; i < ctx->color_cache_count; i++) {
        if (ctx->color_cache[i].r == color.r &&
            ctx->color_cache[i].g == color.g &&
            ctx->color_cache[i].b == color.b) {
            return ctx->color_cache[i].pixel;
        }
    }

    colormap = DefaultColormap(ctx->display, ctx->screen);

    xcolor.red = color.r << 8;
    xcolor.green = color.g << 8;
    xcolor.blue = color.b << 8;
    xcolor.flags = DoRed | DoGreen | DoBlue;

    if (XAllocColor(ctx->display, colormap, &xcolor)) {
        if (ctx->color_cache_count < COLOR_CACHE_SIZE) {
            ctx->color_cache[ctx->color_cache_count].r = color.r;
            ctx->color_cache[ctx->color_cache_count].g = color.g;
            ctx->color_cache[ctx->color_cache_count].b = color.b;
            ctx->color_cache[ctx->color_cache_count].pixel = xcolor.pixel;
            ctx->color_cache_count++;
        }
        return xcolor.pixel;
    }

    brightness = (color.r + color.g + color.b) / 3;
    if (brightness < 128) {
        return BlackPixel(ctx->display, ctx->screen);
    } else {
        return WhitePixel(ctx->display, ctx->screen);
    }
}

int graphics_init(void) {
    if (x11_initialized) {
        return 1;
    }

    fps_last_time = x11_get_time_ms();
    frame_count = 0;
    current_fps = 0.0f;

    x11_initialized = 1;
    return 1;
}

void graphics_cleanup(void) {
    if (!x11_initialized) return;
    x11_initialized = 0;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    window_t *window;
    x11_window_context_t *ctx;
    Window root;

    window = malloc(sizeof(window_t));
    if (!window) return NULL;

    ctx = malloc(sizeof(x11_window_context_t));
    if (!ctx) {
        free(window);
        return NULL;
    }

    ctx->display = XOpenDisplay(NULL);
    if (!ctx->display) {
        free(ctx);
        free(window);
        return NULL;
    }

    ctx->screen = DefaultScreen(ctx->display);
    ctx->width = width;
    ctx->height = height;
    ctx->should_quit = 0;
    ctx->color_cache_count = 0;

    root = RootWindow(ctx->display, ctx->screen);
    ctx->bg_color = WhitePixel(ctx->display, ctx->screen);

    ctx->window = XCreateSimpleWindow(ctx->display, root, 0, 0, width, height, 1,
                                     BlackPixel(ctx->display, ctx->screen), ctx->bg_color);

    XStoreName(ctx->display, ctx->window, title);

    ctx->wm_delete_window = XInternAtom(ctx->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ctx->display, ctx->window, &ctx->wm_delete_window, 1);

    XSelectInput(ctx->display, ctx->window,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask | PointerMotionMask);

    ctx->pixmap = XCreatePixmap(ctx->display, ctx->window, width, height,
                               DefaultDepth(ctx->display, ctx->screen));

    ctx->gc = XCreateGC(ctx->display, ctx->pixmap, 0, NULL);
    XSetBackground(ctx->display, ctx->gc, ctx->bg_color);
    XSetForeground(ctx->display, ctx->gc, BlackPixel(ctx->display, ctx->screen));

    ctx->font_info = XLoadQueryFont(ctx->display, "fixed");
    if (!ctx->font_info) {
        ctx->font_info = XLoadQueryFont(ctx->display, "*");
    }
    if (ctx->font_info) {
        XSetFont(ctx->display, ctx->gc, ctx->font_info->fid);
    }

    XFillRectangle(ctx->display, ctx->pixmap, ctx->gc, 0, 0, width, height);

    XMapWindow(ctx->display, ctx->window);
    XFlush(ctx->display);

    x11_active_window = ctx;

    window->handle = ctx;
    return window;
}

void window_destroy(window_t *window) {
    x11_window_context_t *ctx;

    if (!window) return;

    ctx = (x11_window_context_t*)window->handle;
    if (ctx) {
        if (x11_active_window == ctx) {
            x11_active_window = NULL;
        }

        if (ctx->font_info) {
            XFreeFont(ctx->display, ctx->font_info);
        }
        if (ctx->gc) {
            XFreeGC(ctx->display, ctx->gc);
        }
        if (ctx->pixmap) {
            XFreePixmap(ctx->display, ctx->pixmap);
        }
        if (ctx->window) {
            XDestroyWindow(ctx->display, ctx->window);
        }
        if (ctx->display) {
            XCloseDisplay(ctx->display);
        }
        free(ctx);
    }
    free(window);
}

void window_set_fullscreen(window_t *window, int fullscreen) {
    x11_window_context_t *ctx;
    XEvent event;

    if (!window) return;

    ctx = (x11_window_context_t*)window->handle;

    if (fullscreen) {
        event.type = ClientMessage;
        event.xclient.window = ctx->window;
        event.xclient.message_type = XInternAtom(ctx->display, "_NET_WM_STATE", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1;
        event.xclient.data.l[1] = XInternAtom(ctx->display, "_NET_WM_STATE_FULLSCREEN", False);
        event.xclient.data.l[2] = 0;

        XSendEvent(ctx->display, DefaultRootWindow(ctx->display), False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);
    } else {
        event.type = ClientMessage;
        event.xclient.window = ctx->window;
        event.xclient.message_type = XInternAtom(ctx->display, "_NET_WM_STATE", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 0;
        event.xclient.data.l[1] = XInternAtom(ctx->display, "_NET_WM_STATE_FULLSCREEN", False);
        event.xclient.data.l[2] = 0;

        XSendEvent(ctx->display, DefaultRootWindow(ctx->display), False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);
    }

    XFlush(ctx->display);
    fullscreen_state = fullscreen;
}

int window_is_fullscreen(window_t *window) {
    x11_window_context_t *ctx;
    Atom net_wm_state;
    Atom net_wm_state_fullscreen;
    Atom type_return;
    int format_return;
    unsigned long nitems_return;
    unsigned long bytes_after_return;
    unsigned char *prop_return;
    Atom *atoms;
    unsigned long i;

    if (!window) return 0;

    ctx = (x11_window_context_t*)window->handle;
    net_wm_state = XInternAtom(ctx->display, "_NET_WM_STATE", False);
    net_wm_state_fullscreen = XInternAtom(ctx->display, "_NET_WM_STATE_FULLSCREEN", False);

    if (XGetWindowProperty(ctx->display, ctx->window, net_wm_state, 0, 1024, False,
                          XA_ATOM, &type_return, &format_return, &nitems_return,
                          &bytes_after_return, &prop_return) == Success) {

        atoms = (Atom*)prop_return;
        for (i = 0; i < nitems_return; i++) {
            if (atoms[i] == net_wm_state_fullscreen) {
                XFree(prop_return);
                return 1;
            }
        }
        XFree(prop_return);
    }

    return 0;
}

void window_set_topmost(window_t *window, int topmost) {
    x11_window_context_t *ctx;
    Atom net_wm_state;
    Atom net_wm_state_above;
    XEvent event;

    if (!window) return;

    ctx = (x11_window_context_t*)window->handle;
    net_wm_state = XInternAtom(ctx->display, "_NET_WM_STATE", False);
    net_wm_state_above = XInternAtom(ctx->display, "_NET_WM_STATE_ABOVE", False);
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = ctx->window;
    event.xclient.message_type = net_wm_state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = topmost ? 1 : 0;
    event.xclient.data.l[1] = net_wm_state_above;
    event.xclient.data.l[2] = 0;

    XSendEvent(ctx->display, DefaultRootWindow(ctx->display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &event);
    XFlush(ctx->display);
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    x11_window_context_t *ctx;
    Window root;
    int x, y;
    unsigned int w, h, border_width, depth;

    if (!window || !width || !height) return;

    ctx = (x11_window_context_t*)window->handle;

    if (XGetGeometry(ctx->display, ctx->window, &root, &x, &y, &w, &h, &border_width, &depth)) {
        if ((int)w != ctx->width || (int)h != ctx->height) {
            ctx->width = w;
            ctx->height = h;

            XFreePixmap(ctx->display, ctx->pixmap);
            ctx->pixmap = XCreatePixmap(ctx->display, ctx->window, ctx->width, ctx->height,
                                       DefaultDepth(ctx->display, ctx->screen));

            XSetForeground(ctx->display, ctx->gc, ctx->bg_color);
            XFillRectangle(ctx->display, ctx->pixmap, ctx->gc, 0, 0, ctx->width, ctx->height);
        }

        *width = ctx->width;
        *height = ctx->height;
    } else {
        *width = ctx->width;
        *height = ctx->height;
    }
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *renderer;
    x11_renderer_context_t *ctx;

    renderer = malloc(sizeof(renderer_t));
    if (!renderer) return NULL;

    ctx = malloc(sizeof(x11_renderer_context_t));
    if (!ctx) {
        free(renderer);
        return NULL;
    }

    ctx->window_context = (x11_window_context_t*)window->handle;
    ctx->current_color = BlackPixel(ctx->window_context->display, ctx->window_context->screen);
    ctx->last_set_color = ctx->current_color;

    renderer->handle = ctx;
    return renderer;
}

void renderer_destroy(renderer_t *renderer) {
    x11_renderer_context_t *ctx;

    if (!renderer) return;

    ctx = (x11_renderer_context_t*)renderer->handle;
    if (ctx) {
        free(ctx);
    }
    free(renderer);
}

void renderer_clear(renderer_t *renderer, color_t color) {
    x11_renderer_context_t *ctx;
    unsigned long x11_color;

    if (!renderer) return;

    ctx = (x11_renderer_context_t*)renderer->handle;
    x11_color = x11_create_color_cached(ctx->window_context, color);

    if (x11_color != ctx->last_set_color) {
        XSetForeground(ctx->window_context->display, ctx->window_context->gc, x11_color);
        ctx->last_set_color = x11_color;
    }
    XFillRectangle(ctx->window_context->display, ctx->window_context->pixmap,
                   ctx->window_context->gc, 0, 0,
                   ctx->window_context->width, ctx->window_context->height);
}

void renderer_present(renderer_t *renderer) {
    x11_renderer_context_t *ctx;

    if (!renderer) return;

    ctx = (x11_renderer_context_t*)renderer->handle;

    XCopyArea(ctx->window_context->display,
              ctx->window_context->pixmap,
              ctx->window_context->window,
              ctx->window_context->gc,
              0, 0,
              ctx->window_context->width, ctx->window_context->height,
              0, 0);

    XFlush(ctx->window_context->display);
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    x11_renderer_context_t *ctx;

    if (!renderer) return;

    ctx = (x11_renderer_context_t*)renderer->handle;
    ctx->current_color = x11_create_color_cached(ctx->window_context, color);
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    x11_renderer_context_t *ctx;
    XSegment seg;

    if (!renderer) return;

    ctx = (x11_renderer_context_t*)renderer->handle;
    if (ctx->current_color != ctx->last_set_color) {
        XSetForeground(ctx->window_context->display, ctx->window_context->gc, ctx->current_color);
        ctx->last_set_color = ctx->current_color;
    }

    seg.x1 = x1;
    seg.y1 = y1;
    seg.x2 = x2;
    seg.y2 = y2;
    XDrawSegments(ctx->window_context->display, ctx->window_context->pixmap,
                  ctx->window_context->gc, &seg, 1);
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    x11_renderer_context_t *ctx;

    if (!renderer) return;

    ctx = (x11_renderer_context_t*)renderer->handle;
    if (ctx->current_color != ctx->last_set_color) {
        XSetForeground(ctx->window_context->display, ctx->window_context->gc, ctx->current_color);
        ctx->last_set_color = ctx->current_color;
    }
    XDrawRectangle(ctx->window_context->display, ctx->window_context->pixmap,
                   ctx->window_context->gc, rect.x, rect.y, rect.w, rect.h);
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    x11_renderer_context_t *ctx;

    if (!renderer) return;

    ctx = (x11_renderer_context_t*)renderer->handle;
    if (ctx->current_color != ctx->last_set_color) {
        XSetForeground(ctx->window_context->display, ctx->window_context->gc, ctx->current_color);
        ctx->last_set_color = ctx->current_color;
    }
    XFillRectangle(ctx->window_context->display, ctx->window_context->pixmap,
                   ctx->window_context->gc, rect.x, rect.y, rect.w, rect.h);
}

font_t *font_create(const char *path, int32_t size) {
    font_t *font;
    x11_font_context_t *ctx;
    char font_pattern[256];
    const char *fixed_fonts[4];
    int num_fonts;
    int i;

    if (path && strlen(path) > 0) {
    }

    font = malloc(sizeof(font_t));
    if (!font) return NULL;

    ctx = malloc(sizeof(x11_font_context_t));
    if (!ctx) {
        free(font);
        return NULL;
    }

    ctx->display = x11_active_window ? x11_active_window->display : NULL;
    if (!ctx->display) {
        free(ctx);
        free(font);
        return NULL;
    }

    if (path && strlen(path) > 0) {
        ctx->font_struct = XLoadQueryFont(ctx->display, path);
        if (ctx->font_struct) {
            font->handle = ctx;
            return font;
        } else {
        }
    }

    fixed_fonts[0] = NULL;
    fixed_fonts[1] = NULL;
    fixed_fonts[2] = NULL;
    fixed_fonts[3] = NULL;
    num_fonts = 0;

    if (size <= 10) {
        fixed_fonts[0] = "6x10";
        fixed_fonts[1] = "fixed";
        fixed_fonts[2] = "6x9";
        num_fonts = 3;
    } else if (size <= 13) {
        fixed_fonts[0] = "6x13";
        fixed_fonts[1] = "7x13";
        fixed_fonts[2] = "fixed";
        num_fonts = 3;
    } else if (size <= 16) {
        fixed_fonts[0] = "8x16";
        fixed_fonts[1] = "9x15";
        fixed_fonts[2] = "7x14";
        fixed_fonts[3] = "6x13";
        num_fonts = 4;
    } else {
        fixed_fonts[0] = "9x18";
        fixed_fonts[1] = "8x16";
        fixed_fonts[2] = "9x15";
        fixed_fonts[3] = "7x14";
        num_fonts = 4;
    }

    for (i = 0; i < num_fonts; i++) {
        ctx->font_struct = XLoadQueryFont(ctx->display, fixed_fonts[i]);
        if (ctx->font_struct) {
            break;
        }
    }

    if (!ctx->font_struct) {
        const char *fallbacks[] = {"fixed", "6x13", "cursor"};
        for (i = 0; i < 3; i++) {
            ctx->font_struct = XLoadQueryFont(ctx->display, fallbacks[i]);
            if (ctx->font_struct) {
                break;
            }
        }
    }

    if (!ctx->font_struct) {
        ctx->font_struct = XLoadQueryFont(ctx->display, "*");
    }

    if (!ctx->font_struct) {
        free(ctx);
        free(font);
        return NULL;
    }

    font->handle = ctx;
    return font;
}

void font_destroy(font_t *font) {
    x11_font_context_t *ctx;

    if (!font) return;

    ctx = (x11_font_context_t*)font->handle;
    if (ctx) {
        if (ctx->font_struct && ctx->display) {
            XFreeFont(ctx->display, ctx->font_struct);
        }
        free(ctx);
    }
    free(font);
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    x11_renderer_context_t *rctx;
    x11_font_context_t *fctx;
    unsigned long text_color;

    if (!renderer || !font || !text) return;

    rctx = (x11_renderer_context_t*)renderer->handle;
    fctx = (x11_font_context_t*)font->handle;

    if (!rctx || !fctx) return;
    if (!fctx->font_struct) return;

    text_color = x11_create_color_cached(rctx->window_context, color);

    if (text_color != rctx->last_set_color) {
        XSetForeground(rctx->window_context->display, rctx->window_context->gc, text_color);
        rctx->last_set_color = text_color;
    }
    XSetFont(rctx->window_context->display, rctx->window_context->gc, fctx->font_struct->fid);

    y += fctx->font_struct->ascent;

    XDrawString(rctx->window_context->display, rctx->window_context->pixmap,
                rctx->window_context->gc, x, y, text, strlen(text));
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    x11_font_context_t *ctx;

    if (!font || !text || !width || !height) return;

    ctx = (x11_font_context_t*)font->handle;
    if (ctx->font_struct) {
        *width = XTextWidth(ctx->font_struct, text, strlen(text));
        *height = ctx->font_struct->ascent + ctx->font_struct->descent;
    } else {
        *width = 0;
        *height = 0;
    }
}

int graphics_poll_events(void) {
    if (!x11_active_window) return 1;

    while (XPending(x11_active_window->display)) {
        XEvent event;
        XNextEvent(x11_active_window->display, &event);

        switch (event.type) {
            case Expose:
                break;
            case ConfigureNotify:
                if (event.xconfigure.width != x11_active_window->width ||
                    event.xconfigure.height != x11_active_window->height) {
                    x11_active_window->width = event.xconfigure.width;
                    x11_active_window->height = event.xconfigure.height;
                    window_resized = 1;

                    XFreePixmap(x11_active_window->display, x11_active_window->pixmap);
                    x11_active_window->pixmap = XCreatePixmap(x11_active_window->display,
                                                             x11_active_window->window,
                                                             x11_active_window->width,
                                                             x11_active_window->height,
                                                             DefaultDepth(x11_active_window->display,
                                                                         x11_active_window->screen));

                    XSetForeground(x11_active_window->display, x11_active_window->gc,
                                  x11_active_window->bg_color);
                    XFillRectangle(x11_active_window->display, x11_active_window->pixmap,
                                  x11_active_window->gc, 0, 0,
                                  x11_active_window->width, x11_active_window->height);
                }
                break;
            case KeyPress: {
                KeySym key;
                key = XLookupKeysym(&event.xkey, 0);
                switch (key) {
                    case XK_q:
                        pending_event.type = GRAPHICS_EVENT_QUIT;
                        pending_event.key = KEY_Q;
                        break;
                    case XK_r:
                        pending_event.type = GRAPHICS_EVENT_REFRESH;
                        pending_event.key = KEY_R;
                        break;
                    case XK_f:
                        pending_event.type = GRAPHICS_EVENT_FULLSCREEN_TOGGLE;
                        pending_event.key = KEY_F;
                        break;
                    case XK_Escape:
                        return 0;
                }
                break;
            }
            case MotionNotify:
                current_mouse_x = event.xmotion.x;
                current_mouse_y = event.xmotion.y;
                last_mouse_motion_time = x11_get_time_ms();
                pending_event.type = GRAPHICS_EVENT_MOUSE_MOTION;
                pending_event.mouse_x = current_mouse_x;
                pending_event.mouse_y = current_mouse_y;
                break;
            case ClientMessage:
                if ((unsigned long)event.xclient.data.l[0] == x11_active_window->wm_delete_window) {
                    x11_active_window->should_quit = 1;
                    return 0;
                }
                break;
        }
    }

    return !x11_active_window->should_quit;
}

int graphics_wait_events(void) {
    extern int config_get_max_fps(void);
    int fps;
    int sleep_us;
    uint64_t current_time;
    uint64_t time_since_mouse_motion;

    if (!x11_active_window) return 1;

    if (x11_active_window->should_quit) {
        return 0;
    }

    fps = config_get_max_fps();
    if (fps <= 0) fps = 1;

    current_time = x11_get_time_ms();
    time_since_mouse_motion = current_time - last_mouse_motion_time;

    if (time_since_mouse_motion < 1000) {
        sleep_us = 16000;
    } else {
        sleep_us = 1000000 / fps;
    }

    usleep(sleep_us);

    return graphics_poll_events();
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
    (void)fps;
}

void graphics_stop_render_timer(void) {
}

int window_was_resized(void) {
    int result;
    result = window_resized;
    window_resized = 0;
    return result;
}

void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, int enabled) {
    uint64_t current_time;
    char fps_text[32];
    color_t fps_bg_color;
    color_t fps_text_color;
    rect_t fps_bg_rect;

    if (!enabled || !renderer || !font) return;

    frame_count++;
    current_time = x11_get_time_ms();

    if (current_time - fps_last_time >= 1000) {
        current_fps = (float)frame_count * 1000.0f / (float)(current_time - fps_last_time);
        frame_count = 0;
        fps_last_time = current_time;
    }

    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", current_fps);
    fps_bg_color = (color_t){0, 0, 0, 180};
    fps_text_color = (color_t){255, 255, 0, 255};
    fps_bg_rect = (rect_t){5, 5, 80, 20};

    renderer_set_color(renderer, fps_bg_color);
    renderer_fill_rect(renderer, fps_bg_rect);

    font_draw_text(renderer, font, fps_text_color, 10, 8, fps_text);
}

void graphics_get_mouse_position(int32_t *x, int32_t *y) {
    if (x) *x = current_mouse_x;
    if (y) *y = current_mouse_y;
}
