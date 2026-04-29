#include "../graphics.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_CLASS_NAME "SNGWindowClass"

typedef struct {
    HWND hwnd;
    int fullscreen;
} win32_window_t;

typedef struct {
    HDC hdc;
    HDC mem_dc;
    HBITMAP mem_bitmap;
    HBITMAP old_bitmap;
    int width;
    int height;
    HWND hwnd;
    HPEN current_pen;
    HBRUSH current_brush;
} win32_renderer_t;

typedef struct {
    HFONT hfont;
    int size;
} win32_font_t;

static int gdi_initialized = 0;
static int window_resized = 0;
static graphics_event_t pending_event = {GRAPHICS_EVENT_NONE, 0, 0, 0};
static int32_t current_mouse_x = 0;
static int32_t current_mouse_y = 0;
static unsigned int timer_id = 0;
static int current_fps = 60;

static uint32_t frame_count = 0;
static uint32_t fps_last_time = 0;
static float fps_value = 0.0f;

extern int config_get_max_fps(void);

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
            pending_event.type = GRAPHICS_EVENT_QUIT;
            return 0;

        case WM_SIZE:
            window_resized = 1;
            return 0;

        case WM_KEYDOWN:
            switch (wParam) {
                case 'Q':
                    pending_event.type = GRAPHICS_EVENT_QUIT;
                    pending_event.key = KEY_Q;
                    break;
                case 'R':
                    pending_event.type = GRAPHICS_EVENT_REFRESH;
                    pending_event.key = KEY_R;
                    break;
                case 'F':
                    pending_event.type = GRAPHICS_EVENT_FULLSCREEN_TOGGLE;
                    pending_event.key = KEY_F;
                    break;
            }
            return 0;

        case WM_MOUSEMOVE:
            current_mouse_x = LOWORD(lParam);
            current_mouse_y = HIWORD(lParam);
            pending_event.type = GRAPHICS_EVENT_MOUSE_MOTION;
            pending_event.mouse_x = current_mouse_x;
            pending_event.mouse_y = current_mouse_y;
            return 0;

        case WM_TIMER:
            if (wParam == 1) {
                pending_event.type = GRAPHICS_EVENT_REFRESH;
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

int graphics_init(void) {
    WNDCLASS wc;

    if (gdi_initialized) return 1;

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    if (!RegisterClass(&wc)) {
        return 0;
    }

    fps_last_time = GetTickCount();
    frame_count = 0;
    fps_value = 0.0f;

    gdi_initialized = 1;
    return 1;
}

void graphics_cleanup(void) {
    if (!gdi_initialized) return;
    UnregisterClass(WINDOW_CLASS_NAME, GetModuleHandle(NULL));
    gdi_initialized = 0;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    win32_window_t *win;
    HWND hwnd;
    DWORD style;
    RECT rect;

    win = (win32_window_t*)malloc(sizeof(win32_window_t));
    if (!win) return NULL;

    style = WS_OVERLAPPEDWINDOW;
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRect(&rect, style, FALSE);

    hwnd = CreateWindow(
        WINDOW_CLASS_NAME,
        title,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!hwnd) {
        free(win);
        return NULL;
    }

    win->hwnd = hwnd;
    win->fullscreen = 0;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return (window_t*)win;
}

void window_destroy(window_t *window) {
    win32_window_t *win;

    if (!window) return;

    win = (win32_window_t*)window;
    DestroyWindow(win->hwnd);
    free(win);
}

void window_set_fullscreen(window_t *window, int fullscreen) {
    win32_window_t *win;
    DWORD style;

    if (!window) return;

    win = (win32_window_t*)window;

    if (fullscreen) {
        style = WS_POPUP | WS_VISIBLE;
        SetWindowLong(win->hwnd, GWL_STYLE, style);
        SetWindowPos(win->hwnd, HWND_TOP, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN),
                     GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED);
    } else {
        style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        SetWindowLong(win->hwnd, GWL_STYLE, style);
        SetWindowPos(win->hwnd, HWND_TOP, 100, 100, 800, 600,
                     SWP_FRAMECHANGED);
    }

    win->fullscreen = fullscreen;
}

int window_is_fullscreen(window_t *window) {
    win32_window_t *win;

    if (!window) return 0;

    win = (win32_window_t*)window;
    return win->fullscreen;
}

void window_set_topmost(window_t *window, int topmost) {
    win32_window_t *win;

    if (!window) return;

    win = (win32_window_t*)window;
    SetWindowPos(win->hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    win32_window_t *win;
    RECT rect;

    if (!window || !width || !height) return;

    win = (win32_window_t*)window;
    GetClientRect(win->hwnd, &rect);
    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
}

int window_was_resized(void) {
    int result;
    result = window_resized;
    window_resized = 0;
    return result;
}

renderer_t *renderer_create(window_t *window) {
    win32_renderer_t *renderer;
    win32_window_t *win;
    HDC hdc;
    RECT rect;

    if (!window) return NULL;

    win = (win32_window_t*)window;

    renderer = (win32_renderer_t*)malloc(sizeof(win32_renderer_t));
    if (!renderer) return NULL;

    hdc = GetDC(win->hwnd);
    GetClientRect(win->hwnd, &rect);

    renderer->hdc = hdc;
    renderer->mem_dc = CreateCompatibleDC(hdc);
    renderer->width = rect.right - rect.left;
    renderer->height = rect.bottom - rect.top;
    renderer->mem_bitmap = CreateCompatibleBitmap(hdc, renderer->width, renderer->height);
    renderer->old_bitmap = (HBITMAP)SelectObject(renderer->mem_dc, renderer->mem_bitmap);
    renderer->hwnd = win->hwnd;
    renderer->current_pen = NULL;
    renderer->current_brush = NULL;

    return (renderer_t*)renderer;
}

void renderer_destroy(renderer_t *renderer) {
    win32_renderer_t *r;

    if (!renderer) return;

    r = (win32_renderer_t*)renderer;

    if (r->current_pen) DeleteObject(r->current_pen);
    if (r->current_brush) DeleteObject(r->current_brush);
    if (r->old_bitmap) SelectObject(r->mem_dc, r->old_bitmap);
    if (r->mem_bitmap) DeleteObject(r->mem_bitmap);
    if (r->mem_dc) DeleteDC(r->mem_dc);
    if (r->hdc) ReleaseDC(r->hwnd, r->hdc);

    free(r);
}

void renderer_clear(renderer_t *renderer, color_t color) {
    win32_renderer_t *r;
    HBRUSH brush;
    RECT rect;

    if (!renderer) return;

    r = (win32_renderer_t*)renderer;

    brush = CreateSolidBrush(RGB(color.r, color.g, color.b));
    rect.left = 0;
    rect.top = 0;
    rect.right = r->width;
    rect.bottom = r->height;
    FillRect(r->mem_dc, &rect, brush);
    DeleteObject(brush);
}

void renderer_present(renderer_t *renderer) {
    win32_renderer_t *r;
    RECT rect;

    if (!renderer) return;

    r = (win32_renderer_t*)renderer;

    GetClientRect(r->hwnd, &rect);
    if (rect.right != r->width || rect.bottom != r->height) {
        HBITMAP new_bitmap;

        r->width = rect.right - rect.left;
        r->height = rect.bottom - rect.top;

        new_bitmap = CreateCompatibleBitmap(r->hdc, r->width, r->height);
        SelectObject(r->mem_dc, new_bitmap);
        DeleteObject(r->mem_bitmap);
        r->mem_bitmap = new_bitmap;
    }

    BitBlt(r->hdc, 0, 0, r->width, r->height, r->mem_dc, 0, 0, SRCCOPY);
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    win32_renderer_t *r;

    if (!renderer) return;

    r = (win32_renderer_t*)renderer;

    if (r->current_pen) DeleteObject(r->current_pen);
    if (r->current_brush) DeleteObject(r->current_brush);

    r->current_pen = CreatePen(PS_SOLID, 1, RGB(color.r, color.g, color.b));
    r->current_brush = CreateSolidBrush(RGB(color.r, color.g, color.b));

    SelectObject(r->mem_dc, r->current_pen);
    SelectObject(r->mem_dc, r->current_brush);
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    win32_renderer_t *r;

    if (!renderer) return;

    r = (win32_renderer_t*)renderer;

    MoveToEx(r->mem_dc, x1, y1, NULL);
    LineTo(r->mem_dc, x2, y2);
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    win32_renderer_t *r;
    HBRUSH old_brush;

    if (!renderer) return;

    r = (win32_renderer_t*)renderer;

    old_brush = (HBRUSH)SelectObject(r->mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(r->mem_dc, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
    SelectObject(r->mem_dc, old_brush);
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    win32_renderer_t *r;
    RECT r_rect;

    if (!renderer) return;

    r = (win32_renderer_t*)renderer;

    r_rect.left = rect.x;
    r_rect.top = rect.y;
    r_rect.right = rect.x + rect.w;
    r_rect.bottom = rect.y + rect.h;

    FillRect(r->mem_dc, &r_rect, r->current_brush);
}

font_t *font_create(const char *path, int32_t size) {
    win32_font_t *font;
    HFONT hfont;

    (void)path;

    font = (win32_font_t*)malloc(sizeof(win32_font_t));
    if (!font) return NULL;

    hfont = CreateFont(
        size, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        FIXED_PITCH | FF_MODERN,
        "Courier New"
    );

    if (!hfont) {
        free(font);
        return NULL;
    }

    font->hfont = hfont;
    font->size = size;

    return (font_t*)font;
}

void font_destroy(font_t *font) {
    win32_font_t *f;

    if (!font) return;

    f = (win32_font_t*)font;
    DeleteObject(f->hfont);
    free(f);
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    win32_renderer_t *r;
    win32_font_t *f;
    HFONT old_font;

    if (!renderer || !font || !text) return;

    r = (win32_renderer_t*)renderer;
    f = (win32_font_t*)font;

    old_font = (HFONT)SelectObject(r->mem_dc, f->hfont);
    SetTextColor(r->mem_dc, RGB(color.r, color.g, color.b));
    SetBkMode(r->mem_dc, TRANSPARENT);
    TextOut(r->mem_dc, x, y, text, strlen(text));
    SelectObject(r->mem_dc, old_font);
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    win32_font_t *f;
    HDC hdc;
    HFONT old_font;
    SIZE size;

    if (!font || !text) return;

    f = (win32_font_t*)font;

    hdc = GetDC(NULL);
    old_font = (HFONT)SelectObject(hdc, f->hfont);
    GetTextExtentPoint32(hdc, text, strlen(text), &size);
    SelectObject(hdc, old_font);
    ReleaseDC(NULL, hdc);

    if (width) *width = size.cx;
    if (height) *height = size.cy;
}

int graphics_poll_events(void) {
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return 0;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 1;
}

int graphics_wait_events(void) {
    MSG msg;
    DWORD timeout_ms;
    DWORD result;

    timeout_ms = (DWORD)(1000 / current_fps);

    result = MsgWaitForMultipleObjects(0, NULL, FALSE, timeout_ms, QS_ALLINPUT);

    if (result == WAIT_TIMEOUT) {
        pending_event.type = GRAPHICS_EVENT_REFRESH;
        return 1;
    }

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return 0;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

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
    current_fps = fps;
    if (current_fps <= 0) current_fps = 60;
}

void graphics_stop_render_timer(void) {
    if (timer_id) {
        KillTimer(NULL, timer_id);
        timer_id = 0;
    }
}

void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, int enabled) {
    char fps_text[32];
    color_t fps_bg_color;
    color_t fps_text_color;
    rect_t fps_bg_rect;
    uint32_t current_time;

    if (!enabled || !renderer || !font) return;

    frame_count++;
    current_time = GetTickCount();

    if (current_time - fps_last_time >= 1000) {
        fps_value = (float)frame_count * 1000.0f / (float)(current_time - fps_last_time);
        frame_count = 0;
        fps_last_time = current_time;
    }

    sprintf(fps_text, "FPS: %.1f", fps_value);

    fps_bg_color.r = 0;
    fps_bg_color.g = 0;
    fps_bg_color.b = 0;
    fps_bg_color.a = 180;

    fps_text_color.r = 255;
    fps_text_color.g = 255;
    fps_text_color.b = 0;
    fps_text_color.a = 255;

    fps_bg_rect.x = 5;
    fps_bg_rect.y = 5;
    fps_bg_rect.w = 80;
    fps_bg_rect.h = 20;

    renderer_set_color(renderer, fps_bg_color);
    renderer_fill_rect(renderer, fps_bg_rect);

    font_draw_text(renderer, font, fps_text_color, 10, 8, fps_text);
}

void graphics_get_mouse_position(int32_t *x, int32_t *y) {
    if (x) *x = current_mouse_x;
    if (y) *y = current_mouse_y;
}
