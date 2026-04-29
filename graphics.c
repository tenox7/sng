#define _GNU_SOURCE
#include "graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GFX_SDL3
    #include "gfx/sdl3.c"
#elif defined(GFX_SDL2)
    #include "gfx/sdl2.c"
#elif defined(GFX_GTK3)
    #include "gfx/gtk3.c"
#elif defined(GFX_GTK2)
    #include "gfx/gtk2.c"
#elif defined(GFX_X11)
    #include "gfx/x11.c"
#elif defined(GFX_GLFW)
    #include "gfx/glfw.c"
#elif defined(GFX_COCOA)
    /* implemented in gfx/cocoa.m, compiled separately */
#elif defined(GFX_WIN32)
    #include "gfx/win32.c"
#else
    #error "No graphics driver selected. Use -DGFX_SDL3, -DGFX_SDL2, -DGFX_GTK3, -DGFX_GTK2, -DGFX_X11, -DGFX_GLFW, -DGFX_COCOA, or -DGFX_WIN32"
#endif
