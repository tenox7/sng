/* SDL 1.2 backend. SDL 1.2 has no renderer and no text output, so this
 * rasterizes straight into the video surface and uses the embedded 6x9
 * font. No SDL_ttf, no fontconfig. Builds against real SDL 1.2 and
 * against sdl12-compat. */
#include "../graphics.h"
#include "font6x9.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int config_get_max_fps(void);

/* SDL 1.2 has one implicit window, so the window state is file scope and
 * the video surface is refetched from here after every mode change. */
static SDL_Surface *screen = NULL;
static int screen_bpp = 0;
static int win_w = 0, win_h = 0;
static int desk_w = 0, desk_h = 0;
static int sdl_initialized = 0;
static int window_resized = 0;

static graphics_event_t pending_event = {GRAPHICS_EVENT_NONE, 0, 0, 0};
static int32_t current_mouse_x = 0;
static int32_t current_mouse_y = 0;
static Uint32 last_mouse_motion_time = 0;

static uint32_t frame_count = 0;
static Uint32 fps_last_time = 0;
static float current_fps = 0.0f;

typedef struct {
    color_t color;
    Uint32 pixel;
    SDL_PixelFormat *fmt;
} sdl1_renderer_t;

static int set_video(int w, int h, int fullscreen) {
    SDL_Surface *s;
    Uint32 flags;

    flags = SDL_SWSURFACE | (fullscreen ? SDL_FULLSCREEN : SDL_RESIZABLE);
    s = SDL_SetVideoMode(w, h, screen_bpp, flags);
    if (!s) {
        fprintf(stderr, "SDL_SetVideoMode(%dx%d) failed: %s\n", w, h, SDL_GetError());
        return 0;
    }

    screen = s;
    if (!fullscreen) {
        win_w = w;
        win_h = h;
    }
    window_resized = 1;
    return 1;
}

static Uint32 map_color(color_t c) {
    if (!screen) return 0;
    return SDL_MapRGB(screen->format, c.r, c.g, c.b);
}

/* SDL_MapRGB is a linear palette search on 8bpp displays, so the result is
 * cached until the color or the surface format changes. */
static Uint32 renderer_pixel(sdl1_renderer_t *r) {
    if (r->fmt != screen->format) {
        r->fmt = screen->format;
        r->pixel = map_color(r->color);
    }
    return r->pixel;
}

static int surf_lock(SDL_Surface *s) {
    if (!SDL_MUSTLOCK(s)) return 1;
    return SDL_LockSurface(s) == 0;
}

static void surf_unlock(SDL_Surface *s) {
    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
}

static void put_pixel(SDL_Surface *s, int32_t x, int32_t y, Uint32 c) {
    Uint8 *p;

    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;

    p = (Uint8*)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    switch (s->format->BytesPerPixel) {
        case 1: *p = (Uint8)c; break;
        case 2: *(Uint16*)p = (Uint16)c; break;
        case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            p[0] = (Uint8)(c >> 16); p[1] = (Uint8)(c >> 8); p[2] = (Uint8)c;
#else
            p[0] = (Uint8)c; p[1] = (Uint8)(c >> 8); p[2] = (Uint8)(c >> 16);
#endif
            break;
        default: *(Uint32*)p = c; break;
    }
}

static void draw_line(SDL_Surface *s, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, Uint32 c) {
    int32_t dx, dy, sx, sy, err, e2;

    if (x1 == x2) {
        if (y1 > y2) { dy = y1; y1 = y2; y2 = dy; }
        for (; y1 <= y2; y1++) put_pixel(s, x1, y1, c);
        return;
    }
    if (y1 == y2) {
        if (x1 > x2) { dx = x1; x1 = x2; x2 = dx; }
        for (; x1 <= x2; x1++) put_pixel(s, x1, y1, c);
        return;
    }

    dx = (x2 > x1) ? x2 - x1 : x1 - x2;
    dy = (y2 > y1) ? y1 - y2 : y2 - y1;
    sx = (x1 < x2) ? 1 : -1;
    sy = (y1 < y2) ? 1 : -1;
    err = dx + dy;
    for (;;) {
        put_pixel(s, x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

static void draw_text(SDL_Surface *s, int32_t x, int32_t y, int scale,
                      const char *text, Uint32 c) {
    const uint8_t *glyph;
    int32_t cx, row, col, sx, sy;
    unsigned char ch;

    for (cx = x; *text; text++, cx += FONT_W * scale) {
        ch = (unsigned char)*text;
        if (ch < 32 || ch > 126) ch = '?';
        glyph = font6x9[ch - 32];
        for (row = 0; row < FONT_H; row++) {
            for (col = 0; col < FONT_W; col++) {
                if (!((glyph[row] >> col) & 1)) continue;
                if (scale == 1) {
                    put_pixel(s, cx + col, y + row, c);
                    continue;
                }
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++)
                        put_pixel(s, cx + col * scale + sx, y + row * scale + sy, c);
            }
        }
    }
}

int graphics_init(void) {
    const SDL_VideoInfo *vi;

    if (sdl_initialized) return 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 0;
    }

    /* Only valid before the first SDL_SetVideoMode, so grab it now. */
    vi = SDL_GetVideoInfo();
    if (vi) {
        if (vi->vfmt) screen_bpp = vi->vfmt->BitsPerPixel;
#if SDL_VERSION_ATLEAST(1, 2, 10)
        desk_w = vi->current_w;
        desk_h = vi->current_h;
#endif
    }
    if (screen_bpp < 8) screen_bpp = 0;   /* 0 = let SDL pick */

    if (desk_w <= 0 || desk_h <= 0) {
        SDL_Rect **modes = SDL_ListModes(NULL, SDL_SWSURFACE | SDL_FULLSCREEN);
        if (modes && modes != (SDL_Rect**)-1 && modes[0]) {
            desk_w = modes[0]->w;
            desk_h = modes[0]->h;
        } else {
            desk_w = 640;
            desk_h = 480;
        }
    }

    fps_last_time = SDL_GetTicks();
    frame_count = 0;
    current_fps = 0.0f;

    sdl_initialized = 1;
    return 1;
}

void graphics_cleanup(void) {
    if (!sdl_initialized) return;
    SDL_Quit();
    screen = NULL;
    sdl_initialized = 0;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    window_t *window;

    window = malloc(sizeof(window_t));
    if (!window) return NULL;

    SDL_WM_SetCaption(title, title);

    if (!set_video((int)width, (int)height, 0)) {
        free(window);
        return NULL;
    }

    window->handle = &screen;
    return window;
}

void window_destroy(window_t *window) {
    if (!window) return;
    free(window);
}

void window_set_fullscreen(window_t *window, int fullscreen) {
    if (!window || !screen) return;
    if (fullscreen == window_is_fullscreen(window)) return;

    if (fullscreen) {
        set_video(desk_w, desk_h, 1);
    } else {
        set_video(win_w, win_h, 0);
    }
}

int window_is_fullscreen(window_t *window) {
    if (!window || !screen) return 0;
    return (screen->flags & SDL_FULLSCREEN) != 0;
}

/* SDL 1.2 has no always-on-top. */
void window_set_topmost(window_t *window, int topmost) {
    (void)window;
    (void)topmost;
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    if (!window || !screen || !width || !height) return;
    *width = screen->w;
    *height = screen->h;
}

int window_was_resized(void) {
    int result = window_resized;
    window_resized = 0;
    return result;
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *renderer;
    sdl1_renderer_t *ctx;

    if (!window) return NULL;

    renderer = malloc(sizeof(renderer_t));
    if (!renderer) return NULL;

    ctx = malloc(sizeof(sdl1_renderer_t));
    if (!ctx) {
        free(renderer);
        return NULL;
    }

    memset(ctx, 0, sizeof(sdl1_renderer_t));
    renderer->handle = ctx;
    return renderer;
}

void renderer_destroy(renderer_t *renderer) {
    if (!renderer) return;
    free(renderer->handle);
    free(renderer);
}

void renderer_clear(renderer_t *renderer, color_t color) {
    if (!renderer || !screen) return;
    SDL_FillRect(screen, NULL, map_color(color));
}

void renderer_present(renderer_t *renderer) {
    if (!renderer || !screen) return;
    SDL_Flip(screen);
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    sdl1_renderer_t *ctx;

    if (!renderer || !screen) return;

    ctx = (sdl1_renderer_t*)renderer->handle;
    ctx->color = color;
    ctx->fmt = screen->format;
    ctx->pixel = map_color(color);
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    Uint32 c;

    if (!renderer || !screen) return;

    c = renderer_pixel((sdl1_renderer_t*)renderer->handle);
    if (!surf_lock(screen)) return;
    draw_line(screen, x1, y1, x2, y2, c);
    surf_unlock(screen);
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    Uint32 c;
    int32_t x2, y2;

    if (!renderer || !screen || rect.w <= 0 || rect.h <= 0) return;

    c = renderer_pixel((sdl1_renderer_t*)renderer->handle);
    x2 = rect.x + rect.w - 1;
    y2 = rect.y + rect.h - 1;

    if (!surf_lock(screen)) return;
    draw_line(screen, rect.x, rect.y, x2, rect.y, c);
    draw_line(screen, rect.x, y2, x2, y2, c);
    draw_line(screen, rect.x, rect.y, rect.x, y2, c);
    draw_line(screen, x2, rect.y, x2, y2, c);
    surf_unlock(screen);
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    SDL_Rect r;

    if (!renderer || !screen || rect.w <= 0 || rect.h <= 0) return;

    r.x = (Sint16)rect.x;
    r.y = (Sint16)rect.y;
    r.w = (Uint16)rect.w;
    r.h = (Uint16)rect.h;
    SDL_FillRect(screen, &r, renderer_pixel((sdl1_renderer_t*)renderer->handle));
}

/* No TTF here: path is ignored and size only picks an integer scale of the
 * built-in 6x9 font. */
font_t *font_create(const char *path, int32_t size) {
    font_t *font;
    int *scale;

    (void)path;

    font = malloc(sizeof(font_t));
    if (!font) return NULL;

    scale = malloc(sizeof(int));
    if (!scale) {
        free(font);
        return NULL;
    }

    *scale = (size > 0) ? (int)(size / 11) : 1;
    if (*scale < 1) *scale = 1;

    font->handle = scale;
    return font;
}

void font_destroy(font_t *font) {
    if (!font) return;
    free(font->handle);
    free(font);
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    if (!renderer || !font || !text || !screen) return;

    if (!surf_lock(screen)) return;
    draw_text(screen, x, y, *(int*)font->handle, text, map_color(color));
    surf_unlock(screen);
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    int scale;

    if (!font || !text) return;

    scale = *(int*)font->handle;
    if (width) *width = (int32_t)strlen(text) * FONT_W * scale;
    if (height) *height = FONT_H * scale;
}

int graphics_poll_events(void) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return 0;
            case SDL_VIDEORESIZE:
                if (!(screen && (screen->flags & SDL_FULLSCREEN))) {
                    set_video(event.resize.w, event.resize.h, 0);
                }
                break;
            case SDL_VIDEOEXPOSE:
                pending_event.type = GRAPHICS_EVENT_REFRESH;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_q:
                        pending_event.type = GRAPHICS_EVENT_QUIT;
                        pending_event.key = KEY_Q;
                        break;
                    case SDLK_r:
                        pending_event.type = GRAPHICS_EVENT_REFRESH;
                        pending_event.key = KEY_R;
                        break;
                    case SDLK_f:
                        pending_event.type = GRAPHICS_EVENT_FULLSCREEN_TOGGLE;
                        pending_event.key = KEY_F;
                        break;
                    default:
                        break;
                }
                break;
            case SDL_MOUSEMOTION:
                current_mouse_x = event.motion.x;
                current_mouse_y = event.motion.y;
                last_mouse_motion_time = SDL_GetTicks();
                pending_event.type = GRAPHICS_EVENT_MOUSE_MOTION;
                pending_event.mouse_x = current_mouse_x;
                pending_event.mouse_y = current_mouse_y;
                break;
            default:
                break;
        }
    }

    return 1;
}

/* SDL 1.2 has no SDL_WaitEventTimeout, so this sleeps out the frame budget
 * and then drains the queue, like the X11 backend does. */
int graphics_wait_events(void) {
    int fps, sleep_ms;

    fps = config_get_max_fps();
    if (fps <= 0) fps = 1;

    sleep_ms = (SDL_GetTicks() - last_mouse_motion_time < 1000) ? 16 : 1000 / fps;
    if (sleep_ms < 1) sleep_ms = 1;
    SDL_Delay((Uint32)sleep_ms);

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

void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, int enabled) {
    Uint32 current_time;
    char fps_text[32];
    color_t fps_bg_color;
    color_t fps_text_color;
    rect_t fps_bg_rect;

    if (!enabled || !renderer || !font) return;

    frame_count++;
    current_time = SDL_GetTicks();

    if (current_time - fps_last_time >= 1000) {
        current_fps = (float)frame_count * 1000.0f / (float)(current_time - fps_last_time);
        frame_count = 0;
        fps_last_time = current_time;
    }

    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", current_fps);

    fps_bg_color.r = 0; fps_bg_color.g = 0; fps_bg_color.b = 0; fps_bg_color.a = 255;
    fps_text_color.r = 255; fps_text_color.g = 255; fps_text_color.b = 0; fps_text_color.a = 255;
    fps_bg_rect.x = 5; fps_bg_rect.y = 5; fps_bg_rect.w = 80; fps_bg_rect.h = 20;

    renderer_set_color(renderer, fps_bg_color);
    renderer_fill_rect(renderer, fps_bg_rect);

    font_draw_text(renderer, font, fps_text_color, 10, 8, fps_text);
}

void graphics_get_mouse_position(int32_t *x, int32_t *y) {
    if (x) *x = current_mouse_x;
    if (y) *y = current_mouse_y;
}
