#include "../graphics.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ft2build.h>
#include FT_FREETYPE_H

static int glfw_initialized = 0;
static int window_resized = 0;
static double last_frame_time = 0.0;
static int target_fps = 60;
static int vsync_enabled = 1;

static graphics_event_t pending_event = {GRAPHICS_EVENT_NONE, 0, 0, 0};
static int fullscreen_state = 0;
static int32_t current_mouse_x = 0;
static int32_t current_mouse_y = 0;

static uint32_t frame_count = 0;
static double fps_last_time = 0.0;
static float current_fps = 0.0f;

static FT_Library ft_library;
static int ft_initialized = 0;

static void error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void window_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
    window_resized = 1;
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_Q:
                pending_event.type = GRAPHICS_EVENT_QUIT;
                pending_event.key = KEY_Q;
                break;
            case GLFW_KEY_R:
                pending_event.type = GRAPHICS_EVENT_REFRESH;
                pending_event.key = KEY_R;
                break;
            case GLFW_KEY_F:
                pending_event.type = GRAPHICS_EVENT_FULLSCREEN_TOGGLE;
                pending_event.key = KEY_F;
                break;
        }
    }
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    (void)window;

    current_mouse_x = (int32_t)xpos;
    current_mouse_y = (int32_t)ypos;
    pending_event.type = GRAPHICS_EVENT_MOUSE_MOTION;
    pending_event.mouse_x = current_mouse_x;
    pending_event.mouse_y = current_mouse_y;
}

int graphics_init(void) {
    if (glfw_initialized) {
        return 1;
    }

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "GLFW initialization failed\n");
        return 0;
    }

    if (!ft_initialized) {
        if (FT_Init_FreeType(&ft_library)) {
            fprintf(stderr, "FreeType initialization failed\n");
            glfwTerminate();
            return 0;
        }
        ft_initialized = 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    fps_last_time = glfwGetTime();
    frame_count = 0;
    current_fps = 0.0f;
    last_frame_time = glfwGetTime();

    glfw_initialized = 1;
    return 1;
}

void graphics_cleanup(void) {
    if (!glfw_initialized) return;
    if (ft_initialized) {
        FT_Done_FreeType(ft_library);
        ft_initialized = 0;
    }
    glfwTerminate();
    glfw_initialized = 0;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    window_t *window = malloc(sizeof(window_t));
    if (!window) return NULL;

    GLFWwindow* glfw_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!glfw_window) {
        free(window);
        return NULL;
    }

    glfwSetWindowSizeCallback(glfw_window, window_size_callback);
    glfwSetKeyCallback(glfw_window, key_callback);
    glfwSetCursorPosCallback(glfw_window, cursor_position_callback);
    glfwMakeContextCurrent(glfw_window);

    glfwSetWindowSize(glfw_window, width, height);

    if (vsync_enabled) {
        glfwSwapInterval(1);
    } else {
        glfwSwapInterval(0);
    }

    window->handle = glfw_window;
    return window;
}

void window_destroy(window_t *window) {
    if (!window) return;
    glfwDestroyWindow((GLFWwindow*)window->handle);
    free(window);
}

void window_set_fullscreen(window_t *window, int fullscreen) {
    if (!window) return;

    GLFWwindow* glfw_window = (GLFWwindow*)window->handle;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (fullscreen) {
        glfwSetWindowMonitor(glfw_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(glfw_window, NULL, 100, 100, 800, 600, 0);
    }
    fullscreen_state = fullscreen;
}

int window_is_fullscreen(window_t *window) {
    if (!window) return 0;

    GLFWwindow* glfw_window = (GLFWwindow*)window->handle;
    GLFWmonitor* monitor = glfwGetWindowMonitor(glfw_window);
    return monitor != NULL;
}

void window_set_topmost(window_t *window, int topmost) {
    if (!window) return;

    GLFWwindow* glfw_window = (GLFWwindow*)window->handle;
    glfwSetWindowAttrib(glfw_window, GLFW_FLOATING, topmost ? GLFW_TRUE : GLFW_FALSE);
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    if (!window || !width || !height) return;
    glfwGetWindowSize((GLFWwindow*)window->handle, width, height);
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *renderer = malloc(sizeof(renderer_t));
    if (!renderer) return NULL;

    GLFWwindow* glfw_window = (GLFWwindow*)window->handle;
    glfwMakeContextCurrent(glfw_window);

    int32_t width, height;
    glfwGetFramebufferSize(glfw_window, &width, &height);
    glViewport(0, 0, width, height);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    renderer->handle = glfw_window;
    return renderer;
}

void renderer_destroy(renderer_t *renderer) {
    if (!renderer) return;
    free(renderer);
}

void renderer_clear(renderer_t *renderer, color_t color) {
    if (!renderer) return;

    glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void renderer_present(renderer_t *renderer) {
    if (!renderer) return;

    GLFWwindow* glfw_window = (GLFWwindow*)renderer->handle;
    glfwSwapBuffers(glfw_window);
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    if (!renderer) return;
    glColor4f(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (!renderer) return;

    GLFWwindow* glfw_window = (GLFWwindow*)renderer->handle;
    int32_t win_width, win_height;
    glfwGetWindowSize(glfw_window, &win_width, &win_height);
    glViewport(0, 0, win_width, win_height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_width, win_height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_LINES);
    glVertex2i(x1, y1);
    glVertex2i(x2, y2);
    glEnd();
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;

    GLFWwindow* glfw_window = (GLFWwindow*)renderer->handle;
    int32_t win_width, win_height;
    glfwGetWindowSize(glfw_window, &win_width, &win_height);
    glViewport(0, 0, win_width, win_height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_width, win_height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_LINE_LOOP);
    glVertex2i(rect.x, rect.y);
    glVertex2i(rect.x + rect.w, rect.y);
    glVertex2i(rect.x + rect.w, rect.y + rect.h);
    glVertex2i(rect.x, rect.y + rect.h);
    glEnd();
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;

    GLFWwindow* glfw_window = (GLFWwindow*)renderer->handle;
    int32_t win_width, win_height;
    glfwGetWindowSize(glfw_window, &win_width, &win_height);
    glViewport(0, 0, win_width, win_height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_width, win_height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_QUADS);
    glVertex2i(rect.x, rect.y);
    glVertex2i(rect.x + rect.w, rect.y);
    glVertex2i(rect.x + rect.w, rect.y + rect.h);
    glVertex2i(rect.x, rect.y + rect.h);
    glEnd();
}

font_t *font_create(const char *path, int32_t size) {
    if (!ft_initialized) return NULL;

    font_t *font = malloc(sizeof(font_t));
    if (!font) return NULL;

    FT_Face face;
    FT_Error error;

    if (path && strlen(path) > 0) {
        error = FT_New_Face(ft_library, path, 0, &face);
    } else {
        const char* default_paths[] = {
            "/System/Library/Fonts/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/usr/share/fonts/1type/dejavu/DejaVuSans.ttf",
            NULL
        };

        int i;
        error = 1;
        for (i = 0; default_paths[i] && error; i++) {
            error = FT_New_Face(ft_library, default_paths[i], 0, &face);
        }
    }

    if (error) {
        free(font);
        return NULL;
    }

    error = FT_Set_Pixel_Sizes(face, 0, size);
    if (error) {
        FT_Done_Face(face);
        free(font);
        return NULL;
    }

    font->handle = face;
    return font;
}

void font_destroy(font_t *font) {
    if (!font) return;
    if (font->handle) {
        FT_Done_Face((FT_Face)font->handle);
    }
    free(font);
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    if (!renderer || !font || !text || !font->handle) return;

    GLFWwindow* glfw_window = (GLFWwindow*)renderer->handle;
    int32_t win_width, win_height;
    glfwGetWindowSize(glfw_window, &win_width, &win_height);
    glViewport(0, 0, win_width, win_height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_width, 0, win_height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    FT_Face face = (FT_Face)font->handle;
    float pen_x = (float)x;
    float pen_y = (float)(win_height - y - 8);

    for (const char* c = text; *c; c++) {
        if (FT_Load_Char(face, *c, FT_LOAD_RENDER)) {
            continue;
        }

        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap bitmap = slot->bitmap;

        if (bitmap.width == 0 || bitmap.rows == 0) {
            pen_x += slot->advance.x >> 6;
            continue;
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, bitmap.width, bitmap.rows, 0,
                     GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.buffer);

        float xpos = pen_x + slot->bitmap_left;
        float ypos = pen_y - (bitmap.rows - slot->bitmap_top);
        float w = (float)bitmap.width;
        float h = (float)bitmap.rows;

        glColor4f(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);

        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(xpos, ypos + h);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(xpos + w, ypos + h);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(xpos + w, ypos);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(xpos, ypos);
        glEnd();

        glDeleteTextures(1, &texture);
        pen_x += slot->advance.x >> 6;
    }

    glDisable(GL_TEXTURE_2D);
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    if (!font || !font->handle || !text) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    FT_Face face = (FT_Face)font->handle;
    int total_width = 0;
    int max_height = 0;

    for (const char* c = text; *c; c++) {
        if (FT_Load_Char(face, *c, FT_LOAD_DEFAULT)) {
            continue;
        }

        FT_GlyphSlot slot = face->glyph;
        total_width += slot->advance.x >> 6;

        int char_height = slot->bitmap_top + (slot->bitmap.rows - slot->bitmap_top);
        if (char_height > max_height) {
            max_height = char_height;
        }
    }

    if (width) *width = total_width;
    if (height) *height = max_height > 0 ? max_height : face->size->metrics.height >> 6;
}

int graphics_poll_events(void) {
    glfwPollEvents();

    GLFWwindow* current_window = glfwGetCurrentContext();
    if (current_window && glfwWindowShouldClose(current_window)) {
        return 0;
    }

    return 1;
}

int graphics_wait_events(void) {
    extern int config_get_max_fps(void);
    int fps = config_get_max_fps();
    if (fps <= 0) fps = 60;
    target_fps = fps;

    double current_time = glfwGetTime();
    double target_frame_time = 1.0 / target_fps;
    double elapsed = current_time - last_frame_time;

    if (elapsed < target_frame_time) {
        double wait_time = target_frame_time - elapsed;
        if (wait_time > 0.001) {
            glfwWaitEventsTimeout(wait_time);
        } else {
            glfwPollEvents();
        }
    } else {
        glfwPollEvents();
    }

    GLFWwindow* current_window = glfwGetCurrentContext();
    if (current_window && glfwWindowShouldClose(current_window)) {
        return 0;
    }

    current_time = glfwGetTime();
    last_frame_time = current_time;

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
    target_fps = fps > 0 ? fps : 60;
    last_frame_time = glfwGetTime();
}

void graphics_stop_render_timer(void) {
}

int window_was_resized(void) {
    int result = window_resized;
    window_resized = 0;
    return result;
}

void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, int enabled) {
    if (!enabled || !renderer || !font) return;

    frame_count++;
    double current_time = glfwGetTime();

    if (current_time - fps_last_time >= 1.0) {
        current_fps = (float)frame_count / (float)(current_time - fps_last_time);
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
}

void graphics_get_mouse_position(int32_t *x, int32_t *y) {
    if (x) *x = current_mouse_x;
    if (y) *y = current_mouse_y;
}