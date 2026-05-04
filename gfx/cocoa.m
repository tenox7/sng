#include "../graphics.h"
#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifndef MAC_OS_X_VERSION_10_12
#define MAC_OS_X_VERSION_10_12 101200
#endif
#if !defined(MAC_OS_X_VERSION_MAX_ALLOWED) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_12
#define NSWindowStyleMaskTitled NSTitledWindowMask
#define NSWindowStyleMaskClosable NSClosableWindowMask
#define NSWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
#define NSWindowStyleMaskResizable NSResizableWindowMask
#define NSEventMaskAny NSAnyEventMask
#define NSCompositingOperationCopy NSCompositeCopy
#endif

extern int config_get_max_fps(void);

static int initialized = 0;
static int window_resized = 0;
static graphics_event_t pending_event = {GRAPHICS_EVENT_NONE, 0, 0, 0};
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;
static uint32_t frame_count = 0;
static double fps_last_time = 0.0;
static float current_fps = 0.0f;

@interface SNGView : NSView {
    NSImage *backing;
}
- (void)setBacking:(NSImage *)image;
@end

@implementation SNGView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }
- (void)setBacking:(NSImage *)image {
    [image retain];
    [backing release];
    backing = image;
}
- (void)dealloc {
    [backing release];
    [super dealloc];
}
- (void)drawRect:(NSRect)rect {
    (void)rect;
    if (!backing) {
        [[NSColor blackColor] set];
        NSRectFill([self bounds]);
        return;
    }
    [backing drawInRect:[self bounds]
               fromRect:NSZeroRect
              operation:NSCompositingOperationCopy
               fraction:1.0];
}
- (void)keyDown:(NSEvent *)event {
    NSString *chars;
    unichar c;
    chars = [event charactersIgnoringModifiers];
    if ([chars length] == 0) return;
    c = [chars characterAtIndex:0];
    switch (c) {
        case 'q': case 'Q':
            pending_event.type = GRAPHICS_EVENT_QUIT;
            pending_event.key = KEY_Q;
            return;
        case 'r': case 'R':
            pending_event.type = GRAPHICS_EVENT_REFRESH;
            pending_event.key = KEY_R;
            return;
        case 'f': case 'F':
            pending_event.type = GRAPHICS_EVENT_FULLSCREEN_TOGGLE;
            pending_event.key = KEY_F;
            return;
    }
}
- (void)mouseMoved:(NSEvent *)event {
    NSPoint p;
    NSRect b;
    p = [self convertPoint:[event locationInWindow] fromView:nil];
    b = [self bounds];
    mouse_x = (int32_t)p.x;
    mouse_y = (int32_t)(b.size.height - p.y);
    pending_event.type = GRAPHICS_EVENT_MOUSE_MOTION;
    pending_event.mouse_x = mouse_x;
    pending_event.mouse_y = mouse_y;
}
- (void)mouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}
@end

@interface SNGDelegate : NSObject
@end

@implementation SNGDelegate
- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    pending_event.type = GRAPHICS_EVENT_QUIT;
    return NO;
}
- (void)windowDidResize:(NSNotification *)note {
    (void)note;
    window_resized = 1;
}
@end

typedef struct {
    NSWindow *nswindow;
    SNGView *view;
    SNGDelegate *delegate;
    int fullscreen;
    NSRect saved_frame;
} cocoa_window_t;

typedef struct {
    cocoa_window_t *cw;
    NSImage *image;
    int width;
    int height;
    int focused;
    NSBezierPath *line_batch;
    NSColor *cached_color;
    uint32_t cached_color_key;
} cocoa_renderer_t;

typedef struct {
    NSFont *nsfont;
} cocoa_font_t;

static double now_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static void flush_line_batch(cocoa_renderer_t *cr) {
    if (!cr->line_batch) return;
    if ([cr->line_batch elementCount] > 0) [cr->line_batch stroke];
    [cr->line_batch release];
    cr->line_batch = nil;
}

int graphics_init(void) {
    NSAutoreleasePool *pool;
    if (initialized) return 1;
    pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];
    [pool release];
    fps_last_time = now_seconds();
    initialized = 1;
    return 1;
}

void graphics_cleanup(void) {
    initialized = 0;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    NSAutoreleasePool *pool;
    window_t *win;
    cocoa_window_t *cw;
    NSRect frame;
    unsigned int style;
    NSWindow *nsw;
    SNGView *view;
    SNGDelegate *del;
    NSString *titleStr;

    win = malloc(sizeof(window_t));
    if (!win) return NULL;
    cw = malloc(sizeof(cocoa_window_t));
    if (!cw) {
        free(win);
        return NULL;
    }
    memset(cw, 0, sizeof(cocoa_window_t));

    pool = [[NSAutoreleasePool alloc] init];

    frame = NSMakeRect(100, 100, width, height);
    style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
          | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    nsw = [[NSWindow alloc] initWithContentRect:frame
                                      styleMask:style
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
    titleStr = [NSString stringWithUTF8String:title ? title : ""];
    [nsw setTitle:titleStr];
    [nsw setReleasedWhenClosed:NO];
    [nsw setAcceptsMouseMovedEvents:YES];

    view = [[SNGView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    [nsw setContentView:view];
    [nsw makeFirstResponder:view];

    del = [[SNGDelegate alloc] init];
    [nsw setDelegate:del];

    [nsw makeKeyAndOrderFront:nil];

    cw->nswindow = nsw;
    cw->view = view;
    cw->delegate = del;
    cw->fullscreen = 0;
    cw->saved_frame = frame;

    win->handle = cw;

    [pool release];
    return win;
}

void window_destroy(window_t *window) {
    cocoa_window_t *cw;
    NSAutoreleasePool *pool;
    if (!window) return;
    cw = (cocoa_window_t *)window->handle;
    if (!cw) {
        free(window);
        return;
    }
    pool = [[NSAutoreleasePool alloc] init];
    [cw->nswindow setDelegate:nil];
    [cw->nswindow close];
    [cw->nswindow release];
    [cw->delegate release];
    [pool release];
    free(cw);
    free(window);
}

void window_set_fullscreen(window_t *window, int fullscreen) {
    cocoa_window_t *cw;
    NSScreen *screen;
    if (!window) return;
    cw = (cocoa_window_t *)window->handle;
    if (fullscreen && !cw->fullscreen) {
        cw->saved_frame = [cw->nswindow frame];
        screen = [cw->nswindow screen];
        if (!screen) screen = [NSScreen mainScreen];
        [cw->nswindow setFrame:[screen frame] display:YES];
        cw->fullscreen = 1;
        return;
    }
    if (!fullscreen && cw->fullscreen) {
        [cw->nswindow setFrame:cw->saved_frame display:YES];
        cw->fullscreen = 0;
    }
}

int window_is_fullscreen(window_t *window) {
    cocoa_window_t *cw;
    if (!window) return 0;
    cw = (cocoa_window_t *)window->handle;
    return cw->fullscreen;
}

void window_set_topmost(window_t *window, int topmost) {
    cocoa_window_t *cw;
    if (!window) return;
    cw = (cocoa_window_t *)window->handle;
    [cw->nswindow setLevel:(topmost ? NSStatusWindowLevel : NSNormalWindowLevel)];
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    cocoa_window_t *cw;
    NSRect b;
    if (!window || !width || !height) return;
    cw = (cocoa_window_t *)window->handle;
    b = [cw->view bounds];
    *width = (int32_t)b.size.width;
    *height = (int32_t)b.size.height;
}

int window_was_resized(void) {
    int r;
    r = window_resized;
    window_resized = 0;
    return r;
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *r;
    cocoa_renderer_t *cr;
    if (!window) return NULL;
    r = malloc(sizeof(renderer_t));
    if (!r) return NULL;
    cr = malloc(sizeof(cocoa_renderer_t));
    if (!cr) {
        free(r);
        return NULL;
    }
    memset(cr, 0, sizeof(cocoa_renderer_t));
    cr->cw = (cocoa_window_t *)window->handle;
    r->handle = cr;
    return r;
}

void renderer_destroy(renderer_t *renderer) {
    cocoa_renderer_t *cr;
    if (!renderer) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    if (cr) {
        [cr->line_batch release];
        [cr->cached_color release];
        [cr->image release];
        free(cr);
    }
    free(renderer);
}

static void ensure_backing(cocoa_renderer_t *cr) {
    NSRect b;
    int w, h;
    NSImage *img;
    b = [cr->cw->view bounds];
    w = (int)b.size.width;
    h = (int)b.size.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (cr->image && cr->width == w && cr->height == h) return;
    img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
    [cr->image release];
    cr->image = img;
    cr->width = w;
    cr->height = h;
    [cr->cw->view setBacking:img];
}

static void begin_draw(cocoa_renderer_t *cr) {
    if (cr->focused) return;
    ensure_backing(cr);
    [cr->image lockFocus];
    cr->focused = 1;
}

static uint32_t color_key(color_t c) {
    return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16)
         | ((uint32_t)c.b << 8) | (uint32_t)c.a;
}

static NSColor *make_color(cocoa_renderer_t *cr, color_t c) {
    uint32_t key;
    key = color_key(c);
    if (cr && cr->cached_color && cr->cached_color_key == key) {
        return cr->cached_color;
    }
    if (cr) {
        [cr->cached_color release];
        cr->cached_color = [[NSColor colorWithCalibratedRed:(CGFloat)c.r / 255.0
                                                      green:(CGFloat)c.g / 255.0
                                                       blue:(CGFloat)c.b / 255.0
                                                      alpha:(CGFloat)c.a / 255.0] retain];
        cr->cached_color_key = key;
        return cr->cached_color;
    }
    return [NSColor colorWithCalibratedRed:(CGFloat)c.r / 255.0
                                     green:(CGFloat)c.g / 255.0
                                      blue:(CGFloat)c.b / 255.0
                                     alpha:(CGFloat)c.a / 255.0];
}

void renderer_clear(renderer_t *renderer, color_t color) {
    cocoa_renderer_t *cr;
    if (!renderer) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    begin_draw(cr);
    flush_line_batch(cr);
    [make_color(cr, color) set];
    NSRectFill(NSMakeRect(0, 0, cr->width, cr->height));
}

void renderer_present(renderer_t *renderer) {
    cocoa_renderer_t *cr;
    if (!renderer) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    if (cr->focused) {
        flush_line_batch(cr);
        [cr->image unlockFocus];
        cr->focused = 0;
    }
    [cr->cw->view setNeedsDisplay:YES];
    [cr->cw->view displayIfNeeded];
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    cocoa_renderer_t *cr;
    if (!renderer) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    if (!cr->focused) begin_draw(cr);
    flush_line_batch(cr);
    [make_color(cr, color) set];
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    cocoa_renderer_t *cr;
    CGFloat fy1, fy2;
    if (!renderer) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    if (!cr->focused) begin_draw(cr);
    if (!cr->line_batch) {
        cr->line_batch = [[NSBezierPath alloc] init];
        [cr->line_batch setLineWidth:1.0];
    }
    fy1 = (CGFloat)cr->height - (CGFloat)y1 - 0.5;
    fy2 = (CGFloat)cr->height - (CGFloat)y2 - 0.5;
    [cr->line_batch moveToPoint:NSMakePoint((CGFloat)x1 + 0.5, fy1)];
    [cr->line_batch lineToPoint:NSMakePoint((CGFloat)x2 + 0.5, fy2)];
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    cocoa_renderer_t *cr;
    CGFloat fy;
    if (!renderer) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    if (!cr->focused) begin_draw(cr);
    flush_line_batch(cr);
    fy = (CGFloat)cr->height - (CGFloat)rect.y - (CGFloat)rect.h;
    NSFrameRect(NSMakeRect((CGFloat)rect.x + 0.5, fy + 0.5,
                           (CGFloat)rect.w, (CGFloat)rect.h));
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    cocoa_renderer_t *cr;
    CGFloat fy;
    if (!renderer) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    if (!cr->focused) begin_draw(cr);
    flush_line_batch(cr);
    fy = (CGFloat)cr->height - (CGFloat)rect.y - (CGFloat)rect.h;
    NSRectFill(NSMakeRect((CGFloat)rect.x, fy,
                          (CGFloat)rect.w, (CGFloat)rect.h));
}

font_t *font_create(const char *path, int32_t size) {
    font_t *f;
    cocoa_font_t *cf;
    NSFont *nsfont;

    (void)path;

    nsfont = [NSFont userFixedPitchFontOfSize:(CGFloat)size];
    if (!nsfont) nsfont = [NSFont systemFontOfSize:(CGFloat)size];
    if (!nsfont) return NULL;

    f = malloc(sizeof(font_t));
    if (!f) return NULL;
    cf = malloc(sizeof(cocoa_font_t));
    if (!cf) {
        free(f);
        return NULL;
    }
    cf->nsfont = [nsfont retain];
    f->handle = cf;
    return f;
}

void font_destroy(font_t *font) {
    cocoa_font_t *cf;
    if (!font) return;
    cf = (cocoa_font_t *)font->handle;
    if (cf) {
        [cf->nsfont release];
        free(cf);
    }
    free(font);
}

static NSDictionary *text_attrs(cocoa_font_t *cf, color_t color) {
    NSColor *c;
    c = make_color(NULL, color);
    return [NSDictionary dictionaryWithObjectsAndKeys:
            cf->nsfont, NSFontAttributeName,
            c, NSForegroundColorAttributeName,
            nil];
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    cocoa_renderer_t *cr;
    cocoa_font_t *cf;
    NSString *str;
    NSDictionary *attrs;
    NSSize sz;
    CGFloat fy;

    if (!renderer || !font || !text) return;
    cr = (cocoa_renderer_t *)renderer->handle;
    cf = (cocoa_font_t *)font->handle;
    if (!cr->focused) begin_draw(cr);
    flush_line_batch(cr);

    str = [NSString stringWithUTF8String:text];
    if (!str) return;
    attrs = text_attrs(cf, color);
    sz = [str sizeWithAttributes:attrs];
    fy = (CGFloat)cr->height - (CGFloat)y - sz.height;
    [str drawAtPoint:NSMakePoint((CGFloat)x, fy) withAttributes:attrs];
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    cocoa_font_t *cf;
    NSString *str;
    NSDictionary *attrs;
    NSSize sz;
    color_t white = {255, 255, 255, 255};

    if (!font || !text) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    cf = (cocoa_font_t *)font->handle;
    str = [NSString stringWithUTF8String:text];
    if (!str) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    attrs = text_attrs(cf, white);
    sz = [str sizeWithAttributes:attrs];
    if (width) *width = (int32_t)sz.width;
    if (height) *height = (int32_t)sz.height;
}

static void pump_pending(void) {
    NSEvent *event;
    while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES])) {
        [NSApp sendEvent:event];
    }
}

int graphics_poll_events(void) {
    NSAutoreleasePool *pool;
    pool = [[NSAutoreleasePool alloc] init];
    pump_pending();
    [pool release];
    if (pending_event.type == GRAPHICS_EVENT_QUIT) return 0;
    return 1;
}

int graphics_wait_events(void) {
    NSAutoreleasePool *pool;
    NSEvent *event;
    int fps;
    NSDate *until;

    fps = config_get_max_fps();
    if (fps <= 0) fps = 1;

    pool = [[NSAutoreleasePool alloc] init];
    until = [NSDate dateWithTimeIntervalSinceNow:1.0 / (double)fps];
    event = [NSApp nextEventMatchingMask:NSEventMaskAny
                               untilDate:until
                                  inMode:NSDefaultRunLoopMode
                                 dequeue:YES];
    if (event) {
        [NSApp sendEvent:event];
        pump_pending();
    }
    [pool release];

    if (pending_event.type == GRAPHICS_EVENT_QUIT) return 0;
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
    (void)fps;
}

void graphics_stop_render_timer(void) {
}

void graphics_get_mouse_position(int32_t *x, int32_t *y) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
}

void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, int enabled) {
    double current_time;
    char fps_text[32];
    color_t fps_bg = {0, 0, 0, 180};
    color_t fps_fg = {255, 255, 0, 255};
    rect_t bg_rect = {5, 5, 80, 20};

    if (!enabled || !renderer || !font) return;

    frame_count++;
    current_time = now_seconds();
    if (current_time - fps_last_time >= 1.0) {
        current_fps = (float)frame_count / (float)(current_time - fps_last_time);
        frame_count = 0;
        fps_last_time = current_time;
    }
    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", current_fps);

    renderer_set_color(renderer, fps_bg);
    renderer_fill_rect(renderer, bg_rect);
    font_draw_text(renderer, font, fps_fg, 10, 8, fps_text);
}
