/* HTTP server: renders all charts into a single GIF and serves it on a
 * simple HTML 4.01 page with a meta refresh tag. Runs in its own thread,
 * reads the same ring buffers as the local display. No external deps:
 * software rasterizer, embedded 8x8 font, GIF87a/LZW encoder. */
#define _GNU_SOURCE
#include "compat.h"
#include "config.h"
#include "threading.h"
#include "ringbuf.h"
#include "datasource.h"
#include "os/os_interface.h"
#include "httpd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
typedef SOCKET sock_t;
#define close closesocket
#else
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#if defined(__VMS)
typedef unsigned int socklen_t;
#elif (defined(_AIX) && !defined(_AIX43)) || defined(__osf__) || defined(__digital__) || defined(__hpux) || defined(IRIX5)
typedef int socklen_t;
#endif
typedef int sock_t;
#define INVALID_SOCKET (-1)
#endif

#define FONT_W 8
#define FONT_H 8

/* 8x8 bitmap font, ASCII 32-126, public domain (IBM VGA font via
 * Marcel Sondaar / Daniel Hepper font8x8). Byte per row, LSB leftmost. */
static const uint8_t font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}
};

/* ---- software framebuffer, 8-bit indexed ---- */

typedef struct {
    uint8_t *pix;
    int32_t w, h;
    uint8_t pal[256][3];
    int pal_count;
} fb_t;

static uint8_t fb_color(fb_t *fb, color_t c) {
    int i;
    for (i = 0; i < fb->pal_count; i++) {
        if (fb->pal[i][0] == c.r && fb->pal[i][1] == c.g && fb->pal[i][2] == c.b)
            return (uint8_t)i;
    }
    if (fb->pal_count >= 256) return 0;
    fb->pal[fb->pal_count][0] = c.r;
    fb->pal[fb->pal_count][1] = c.g;
    fb->pal[fb->pal_count][2] = c.b;
    return (uint8_t)fb->pal_count++;
}

static void fb_pixel(fb_t *fb, int32_t x, int32_t y, uint8_t ci) {
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    fb->pix[y * fb->w + x] = ci;
}

static void fb_line(fb_t *fb, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t ci) {
    int32_t dx, dy, sx, sy, err, e2;

    if (x1 == x2) {
        if (y1 > y2) { dy = y1; y1 = y2; y2 = dy; }
        for (; y1 <= y2; y1++) fb_pixel(fb, x1, y1, ci);
        return;
    }
    if (y1 == y2) {
        if (x1 > x2) { dx = x1; x1 = x2; x2 = dx; }
        for (; x1 <= x2; x1++) fb_pixel(fb, x1, y1, ci);
        return;
    }

    dx = (x2 > x1) ? x2 - x1 : x1 - x2;
    dy = (y2 > y1) ? y1 - y2 : y2 - y1;
    sx = (x1 < x2) ? 1 : -1;
    sy = (y1 < y2) ? 1 : -1;
    err = dx + dy;
    for (;;) {
        fb_pixel(fb, x1, y1, ci);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

static void fb_rect(fb_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t ci) {
    if (w <= 0 || h <= 0) return;
    fb_line(fb, x, y, x + w - 1, y, ci);
    fb_line(fb, x, y + h - 1, x + w - 1, y + h - 1, ci);
    fb_line(fb, x, y, x, y + h - 1, ci);
    fb_line(fb, x + w - 1, y, x + w - 1, y + h - 1, ci);
}

static void fb_text(fb_t *fb, int32_t x, int32_t y, const char *text, uint8_t ci) {
    int32_t cx, row, col;
    const uint8_t *glyph;
    unsigned char c;

    for (cx = x; *text; text++, cx += FONT_W) {
        c = (unsigned char)*text;
        if (c < 32 || c > 126) c = '?';
        glyph = font8x8[c - 32];
        for (row = 0; row < FONT_H; row++) {
            for (col = 0; col < FONT_W; col++) {
                if ((glyph[row] >> col) & 1)
                    fb_pixel(fb, cx + col, y + row, ci);
            }
        }
    }
}

static int32_t fb_text_width(const char *text) {
    return (int32_t)strlen(text) * FONT_W;
}

/* ---- GIF87a encoder with LZW compression ---- */

typedef struct {
    uint8_t *data;
    uint32_t len, cap;
    int err;
} gbuf_t;

static int gbuf_put(gbuf_t *b, const void *p, uint32_t n) {
    uint8_t *nd;
    uint32_t nc;
    if (b->err) return 0;
    if (b->len + n > b->cap) {
        nc = b->cap ? b->cap * 2 : 16384;
        while (nc < b->len + n) nc *= 2;
        nd = realloc(b->data, nc);
        if (!nd) {
            b->err = 1;
            return 0;
        }
        b->data = nd;
        b->cap = nc;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 1;
}

static int gbuf_byte(gbuf_t *b, uint8_t v) {
    return gbuf_put(b, &v, 1);
}

static int gbuf_u16(gbuf_t *b, uint16_t v) {
    uint8_t le[2];
    le[0] = (uint8_t)(v & 0xFF);
    le[1] = (uint8_t)(v >> 8);
    return gbuf_put(b, le, 2);
}

#define LZW_HSIZE 5003
#define LZW_MAXBITS 12
#define LZW_MAXCODES 4096

typedef struct {
    gbuf_t *out;
    uint32_t cur_accum;
    int cur_bits;
    int n_bits, maxcode;
    int init_bits, clear_code, eof_code, free_ent;
    int clear_flg;
    uint8_t block[255];
    int block_len;
    int32_t htab[LZW_HSIZE];
    uint16_t codetab[LZW_HSIZE];
} lzw_t;

static lzw_t lzw;  /* single http thread, ~35KB kept off its stack */

static void lzw_flush_block(void) {
    if (lzw.block_len == 0) return;
    gbuf_byte(lzw.out, (uint8_t)lzw.block_len);
    gbuf_put(lzw.out, lzw.block, (uint32_t)lzw.block_len);
    lzw.block_len = 0;
}

static void lzw_char_out(uint8_t c) {
    lzw.block[lzw.block_len++] = c;
    if (lzw.block_len >= 255) lzw_flush_block();
}

static void lzw_output(int code) {
    lzw.cur_accum |= (uint32_t)code << lzw.cur_bits;
    lzw.cur_bits += lzw.n_bits;
    while (lzw.cur_bits >= 8) {
        lzw_char_out((uint8_t)(lzw.cur_accum & 0xFF));
        lzw.cur_accum >>= 8;
        lzw.cur_bits -= 8;
    }
    if (lzw.free_ent > lzw.maxcode || lzw.clear_flg) {
        if (lzw.clear_flg) {
            lzw.n_bits = lzw.init_bits;
            lzw.maxcode = (1 << lzw.n_bits) - 1;
            lzw.clear_flg = 0;
        } else {
            lzw.n_bits++;
            lzw.maxcode = (lzw.n_bits == LZW_MAXBITS) ?
                          LZW_MAXCODES : (1 << lzw.n_bits) - 1;
        }
    }
    if (code == lzw.eof_code) {
        while (lzw.cur_bits > 0) {
            lzw_char_out((uint8_t)(lzw.cur_accum & 0xFF));
            lzw.cur_accum >>= 8;
            lzw.cur_bits -= 8;
        }
        lzw_flush_block();
    }
}

static void lzw_clear_hash(void) {
    int i;
    for (i = 0; i < LZW_HSIZE; i++) lzw.htab[i] = -1;
}

static void lzw_compress(gbuf_t *out, int min_code_size, const uint8_t *pix, uint32_t npix) {
    int ent, disp, hshift;
    int32_t fcode, i;
    uint32_t n;
    int c;

    lzw.out = out;
    lzw.cur_accum = 0;
    lzw.cur_bits = 0;
    lzw.block_len = 0;
    lzw.clear_flg = 0;
    lzw.init_bits = min_code_size + 1;
    lzw.n_bits = lzw.init_bits;
    lzw.maxcode = (1 << lzw.n_bits) - 1;
    lzw.clear_code = 1 << min_code_size;
    lzw.eof_code = lzw.clear_code + 1;
    lzw.free_ent = lzw.clear_code + 2;

    hshift = 0;
    for (fcode = LZW_HSIZE; fcode < 65536; fcode *= 2) hshift++;
    hshift = 8 - hshift;

    lzw_clear_hash();
    lzw_output(lzw.clear_code);

    ent = pix[0];
    for (n = 1; n < npix; n++) {
        c = pix[n];
        fcode = ((int32_t)c << LZW_MAXBITS) + ent;
        i = ((int32_t)c << hshift) ^ ent;

        if (lzw.htab[i] == fcode) {
            ent = lzw.codetab[i];
            continue;
        }
        if (lzw.htab[i] >= 0) {
            disp = LZW_HSIZE - i;
            if (i == 0) disp = 1;
            do {
                i -= disp;
                if (i < 0) i += LZW_HSIZE;
                if (lzw.htab[i] == fcode) break;
            } while (lzw.htab[i] >= 0);
            if (lzw.htab[i] == fcode) {
                ent = lzw.codetab[i];
                continue;
            }
        }

        lzw_output(ent);
        ent = c;
        if (lzw.free_ent < LZW_MAXCODES) {
            lzw.codetab[i] = (uint16_t)lzw.free_ent++;
            lzw.htab[i] = fcode;
        } else {
            lzw_clear_hash();
            lzw.free_ent = lzw.clear_code + 2;
            lzw.clear_flg = 1;
            lzw_output(lzw.clear_code);
        }
    }
    lzw_output(ent);
    lzw_output(lzw.eof_code);
}

/* returns malloc'd GIF, caller frees; NULL on alloc failure */
static uint8_t *gif_encode(fb_t *fb, uint32_t *out_len) {
    gbuf_t out;
    int bits, i, pal_size, min_code_size;

    bits = 1;
    while ((1 << bits) < fb->pal_count) bits++;
    if (bits < 2) bits = 2;
    pal_size = 1 << bits;
    min_code_size = bits;

    out.data = NULL;
    out.len = 0;
    out.cap = 0;
    out.err = 0;

    gbuf_put(&out, "GIF87a", 6);
    gbuf_u16(&out, (uint16_t)fb->w);
    gbuf_u16(&out, (uint16_t)fb->h);
    gbuf_byte(&out, (uint8_t)(0xF0 | (bits - 1)));  /* global palette, 8-bit color res */
    gbuf_byte(&out, 0);  /* background index */
    gbuf_byte(&out, 0);  /* aspect */
    for (i = 0; i < pal_size; i++) {
        if (i < fb->pal_count) {
            gbuf_put(&out, fb->pal[i], 3);
        } else {
            gbuf_put(&out, "\0\0\0", 3);
        }
    }
    gbuf_byte(&out, 0x2C);  /* image descriptor */
    gbuf_u16(&out, 0);
    gbuf_u16(&out, 0);
    gbuf_u16(&out, (uint16_t)fb->w);
    gbuf_u16(&out, (uint16_t)fb->h);
    gbuf_byte(&out, 0);  /* no local palette, not interlaced */
    gbuf_byte(&out, (uint8_t)min_code_size);
    lzw_compress(&out, min_code_size, fb->pix, (uint32_t)(fb->w * fb->h));
    gbuf_byte(&out, 0);     /* block terminator */
    gbuf_byte(&out, 0x3B);  /* trailer */

    if (out.err) {
        free(out.data);
        return NULL;
    }
    *out_len = out.len;
    return out.data;
}

/* ---- chart rendering, mirrors plot_draw() layout ---- */

static struct {
    config_t *config;
    data_collector_t *collector;
    sock_t listen_fd;
    volatile int running;
    char hostname[256];
} httpd;

static double snap_vals[2048];
static double snap_vals2[2048];
static uint32_t snap_ts[2048];

static void render_chart(fb_t *fb, uint32_t idx, int32_t x, int32_t y, int32_t width, int32_t height) {
    config_t *config;
    plot_config_t *pc;
    data_source_t *source;
    datasource_handler_t *handler;
    datasource_stats_t stats;
    uint8_t text_ci, border_ci, line_ci, line2_ci, err_ci;
    char title[256], temp[256];
    char *local_pos;
    size_t prefix_len;
    int32_t plot_y, plot_height, plot_bottom, plot_x, plot_max_offset;
    int32_t bar_height, in_bar_height, out_bar_height, out_y;
    int32_t prev_out_x, prev_out_y, pixel_offset;
    uint32_t data_count, data_count2, head, tail, dual_count, i;
    uint32_t now_ms, total_time_ms, minutes, hours, days;
    double max_val, fixed_max_scale, value, in_value, out_value;
    const char *unit;
    char scale_text[64], stats_text[128], time_span_text[64], formatted[64];
    int32_t refresh_interval;

    config = httpd.config;
    pc = &config->plots[idx];
    source = (idx < httpd.collector->source_count) ? &httpd.collector->sources[idx] : NULL;
    handler = (source && source->datasource) ? source->datasource->handler : NULL;

    text_ci = fb_color(fb, config->text_color);
    border_ci = fb_color(fb, config->border_color);
    line_ci = fb_color(fb, pc->line_color);
    line2_ci = fb_color(fb, pc->line_color_secondary);
    err_ci = fb_color(fb, config->error_line_color);

    snprintf(title, sizeof(title), "%s", pc->name);
    if (strstr(title, "local")) {
        local_pos = strstr(title, "local");
        prefix_len = local_pos - title;
        strncpy(temp, title, prefix_len);
        temp[prefix_len] = '\0';
        strcat(temp, httpd.hostname);
        strcat(temp, local_pos + 5);
        snprintf(title, sizeof(title), "%s", temp);
    }
    fb_text(fb, x, y + 5, title, text_ci);

    plot_y = y + 20;
    plot_height = height - 40;
    fb_rect(fb, x, plot_y, width, plot_height, border_ci);

    if (!source || !source->data_buffer || ringbuf_count(source->data_buffer) == 0) {
        fb_text(fb, x, y + height - 15, "No data", text_ci);
        return;
    }

    if (source->datasource) {
        fixed_max_scale = datasource_get_max_scale(source->datasource);
        unit = datasource_get_unit(source->datasource);
    } else {
        fixed_max_scale = 0.0;
        unit = "";
    }

    if (!ringbuf_read_snapshot(source->data_buffer, snap_vals, snap_ts, 2048,
                               &data_count, &head, &tail))
        return;
    data_count2 = 0;
    if (source->is_dual && source->data_buffer_secondary) {
        if (!ringbuf_read_snapshot(source->data_buffer_secondary, snap_vals2, NULL, 2048,
                                   &data_count2, &head, &tail))
            return;
    }

    now_ms = os_get_time_ms();
    refresh_interval = (pc->refresh_interval_ms > 0) ?
                       pc->refresh_interval_ms : config->refresh_interval_ms;
    plot_max_offset = width - 3;
    if (plot_max_offset < 0) plot_max_offset = 0;

    if (fixed_max_scale > 0.0) {
        max_val = fixed_max_scale;
    } else {
        max_val = 0.0;
        for (i = 0; i < data_count; i++) {
            if (snap_vals[i] > max_val) max_val = snap_vals[i];
        }
        for (i = 0; i < data_count2; i++) {
            if (snap_vals2[i] > max_val) max_val = snap_vals2[i];
        }
        if (max_val <= 0.0) max_val = 1.0;
    }

    if (handler && handler->format_value) {
        handler->format_value(max_val, formatted, sizeof(formatted));
        snprintf(scale_text, sizeof(scale_text), "%s", formatted);
    } else if (strlen(unit) > 0) {
        snprintf(scale_text, sizeof(scale_text), "%.1f%s", max_val, unit);
    } else {
        snprintf(scale_text, sizeof(scale_text), "%.1f", max_val);
    }
    fb_text(fb, x + width - fb_text_width(scale_text), y + 5, scale_text, text_ci);

    plot_bottom = plot_y + plot_height - 2;

    if (source->is_dual && source->data_buffer_secondary) {
        prev_out_x = -1;
        prev_out_y = -1;
        dual_count = (data_count < data_count2) ? data_count : data_count2;

        for (i = 0; i < dual_count; i++) {
            in_value = snap_vals[i];
            out_value = snap_vals2[i];

            pixel_offset = (int32_t)((now_ms - snap_ts[i]) / (uint32_t)refresh_interval);
            if (pixel_offset < 0 || pixel_offset > plot_max_offset) {
                prev_out_x = prev_out_y = -1;
                continue;
            }
            plot_x = x + width - 2 - pixel_offset;

            if (in_value < 0 || out_value < 0) {
                fb_line(fb, plot_x, plot_y + 2, plot_x, plot_bottom, err_ci);
                prev_out_x = prev_out_y = -1;
            } else {
                in_bar_height = (int32_t)((in_value / max_val) * (plot_height - 4));
                if (in_bar_height < 1) in_bar_height = 1;
                fb_line(fb, plot_x, plot_bottom - in_bar_height, plot_x, plot_bottom, line_ci);

                out_bar_height = (int32_t)((out_value / max_val) * (plot_height - 4));
                out_y = plot_bottom - out_bar_height;
                if (prev_out_x >= 0 && prev_out_y >= 0) {
                    fb_line(fb, prev_out_x, prev_out_y, plot_x, out_y, line2_ci);
                } else {
                    fb_pixel(fb, plot_x, out_y, line2_ci);
                }
                prev_out_x = plot_x;
                prev_out_y = out_y;
            }
        }
    } else {
        for (i = 0; i < data_count; i++) {
            value = snap_vals[i];

            pixel_offset = (int32_t)((now_ms - snap_ts[i]) / (uint32_t)refresh_interval);
            if (pixel_offset < 0 || pixel_offset > plot_max_offset) continue;
            plot_x = x + width - 2 - pixel_offset;

            if (value < 0) {
                fb_line(fb, plot_x, plot_y + 2, plot_x, plot_bottom, err_ci);
            } else {
                bar_height = (int32_t)((value / max_val) * (plot_height - 4));
                if (bar_height < 1) bar_height = 1;
                fb_line(fb, plot_x, plot_bottom - bar_height, plot_x, plot_bottom, line_ci);
            }
        }
    }

    memset(&stats, 0, sizeof(stats));
    if (handler && handler->get_stats && source->datasource->context) {
        handler->get_stats(source->datasource->context, &stats);
    }

    if (handler && handler->format_value) {
        if (source->is_dual && handler->format_dual_stats) {
            handler->format_dual_stats(stats.last, stats.last_secondary,
                                       stats_text, sizeof(stats_text));
        } else {
            handler->format_value(stats.last, stats_text, sizeof(stats_text));
        }
    } else if (strlen(unit) > 0) {
        snprintf(stats_text, sizeof(stats_text), "%.1f%s", stats.last, unit);
    } else {
        snprintf(stats_text, sizeof(stats_text), "%.1f", stats.last);
    }
    fb_text(fb, x + width - fb_text_width(stats_text), y + height - 15, stats_text, text_ci);

    total_time_ms = source->data_buffer->size * (uint32_t)refresh_interval;
    if (total_time_ms < 60000) {
        snprintf(time_span_text, sizeof(time_span_text), "%us", total_time_ms / 1000);
    } else if (total_time_ms < 86400000) {
        minutes = (total_time_ms + 59999) / 60000;
        if (minutes < 60) {
            snprintf(time_span_text, sizeof(time_span_text), "%um", minutes);
        } else {
            hours = (minutes + 59) / 60;
            snprintf(time_span_text, sizeof(time_span_text), "%uh", hours);
        }
    } else {
        days = (total_time_ms + 86399999) / 86400000;
        snprintf(time_span_text, sizeof(time_span_text), "%ud", days);
    }
    fb_text(fb, x, y + height - 15, time_span_text, text_ci);
}

static uint8_t *render_gif(uint32_t *out_len) {
    fb_t fb;
    config_t *config;
    int32_t plot_height, plot_spacing, margin;
    uint32_t i;
    uint8_t *gif;

    config = httpd.config;
    plot_height = config->default_height;
    plot_spacing = 10;
    margin = config->window_margin;

    fb.w = config->default_width;
    fb.h = (int32_t)config->plot_count * (plot_height + plot_spacing) + margin * 2;
    if (fb.w < 16) fb.w = 16;
    if (fb.h < 16) fb.h = 16;
    if (fb.w > 4096) fb.w = 4096;
    if (fb.h > 4096) fb.h = 4096;
    fb.pix = malloc((size_t)fb.w * fb.h);
    if (!fb.pix) return NULL;
    fb.pal_count = 0;

    memset(fb.pix, fb_color(&fb, config->background_color), (size_t)fb.w * fb.h);

    for (i = 0; i < config->plot_count; i++) {
        render_chart(&fb, i, margin, (int32_t)i * (plot_height + plot_spacing) + margin,
                     fb.w - margin * 2, plot_height);
    }

    gif = gif_encode(&fb, out_len);
    free(fb.pix);
    return gif;
}

/* ---- HTTP server ---- */

static int send_all(sock_t fd, const void *buf, uint32_t len) {
    const char *p = (const char *)buf;
    int n;
    while (len > 0) {
        n = send(fd, p, (int)len, 0);
        if (n <= 0) return 0;
        p += n;
        len -= (uint32_t)n;
    }
    return 1;
}

static void send_response(sock_t fd, const char *status, const char *ctype,
                          const void *body, uint32_t body_len) {
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.0 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %u\r\n"
             "Pragma: no-cache\r\n"
             "Cache-Control: no-cache\r\n"
             "Connection: close\r\n"
             "\r\n",
             status, ctype, (unsigned)body_len);
    if (send_all(fd, header, (uint32_t)strlen(header)))
        send_all(fd, body, body_len);
}

static void serve_html(sock_t fd) {
    char body[1024];
    int32_t refresh_sec;
    color_t bg;

    refresh_sec = httpd.config->refresh_interval_ms / 1000;
    if (refresh_sec < 1) refresh_sec = 1;
    bg = httpd.config->background_color;

    /* cache-buster query on the image, meta refresh reloads the page */
    snprintf(body, sizeof(body),
             "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" "
             "\"http://www.w3.org/TR/html4/loose.dtd\">\r\n"
             "<html><head>\r\n"
             "<meta http-equiv=\"refresh\" content=\"%d\">\r\n"
             "<title>SNG : %s</title>\r\n"
             "</head>\r\n"
             "<body bgcolor=\"#%02X%02X%02X\">\r\n"
             "<img src=\"sng.gif?%u\" alt=\"SNG charts\">\r\n"
             "</body></html>\r\n",
             (int)refresh_sec, httpd.hostname, bg.r, bg.g, bg.b,
             (unsigned)os_get_time_ms());
    send_response(fd, "200 OK", "text/html", body, (uint32_t)strlen(body));
}

static void serve_gif(sock_t fd) {
    uint8_t *gif;
    uint32_t len;

    gif = render_gif(&len);
    if (!gif) {
        send_response(fd, "500 Internal Server Error", "text/html",
                      "<html><body>out of memory</body></html>", 39);
        return;
    }
    send_response(fd, "200 OK", "image/gif", gif, len);
    free(gif);
}

static void handle_client(sock_t fd) {
    char req[1024];
    char *path, *end;
    int n;

    n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    if (strncmp(req, "GET ", 4) != 0) {
        send_response(fd, "501 Not Implemented", "text/html",
                      "<html><body>501</body></html>", 29);
        return;
    }
    path = req + 4;
    end = path;
    while (*end && *end != ' ' && *end != '\r' && *end != '\n' && *end != '?') end++;
    *end = '\0';

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        serve_html(fd);
    } else if (strcmp(path, "/sng.gif") == 0) {
        serve_gif(fd);
    } else {
        send_response(fd, "404 Not Found", "text/html",
                      "<html><body>404</body></html>", 29);
    }
}

static void httpd_thread(void *arg) {
    sock_t client;
    struct sockaddr_in addr;
    socklen_t addr_len;

    (void)arg;
    while (httpd.running) {
        addr_len = sizeof(addr);
        client = accept(httpd.listen_fd, (struct sockaddr *)&addr, &addr_len);
        if (client == INVALID_SOCKET) {
            if (!httpd.running) break;
            os_sleep(100);
            continue;
        }
        handle_client(client);
        close(client);
    }
}

int httpd_start(config_t *config, data_collector_t *collector) {
    struct sockaddr_in addr;
    int opt;
    char *dot;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(1, 1), &wsa);
#endif

    if (!config || !collector) return 0;

    httpd.config = config;
    httpd.collector = collector;

    if (gethostname(httpd.hostname, sizeof(httpd.hostname)) == 0) {
        dot = strchr(httpd.hostname, '.');
        if (dot) *dot = '\0';
    } else {
        strcpy(httpd.hostname, "localhost");
    }

#if defined(SIGPIPE)
    signal(SIGPIPE, SIG_IGN);
#endif

    httpd.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (httpd.listen_fd == INVALID_SOCKET) {
        fprintf(stderr, "httpd: socket() failed\n");
        return 0;
    }

    opt = 1;
    setsockopt(httpd.listen_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)config->http_port);

    if (bind(httpd.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "httpd: cannot bind port %d\n", (int)config->http_port);
        close(httpd.listen_fd);
        return 0;
    }
    if (listen(httpd.listen_fd, 4) < 0) {
        fprintf(stderr, "httpd: listen() failed\n");
        close(httpd.listen_fd);
        return 0;
    }

    httpd.running = 1;
    if (!os_plot_thread_create(httpd_thread, NULL)) {
        fprintf(stderr, "httpd: thread create failed\n");
        close(httpd.listen_fd);
        httpd.running = 0;
        return 0;
    }

    printf("httpd: serving on port %d\n", (int)config->http_port);
    return 1;
}

void httpd_stop(void) {
    if (!httpd.running) return;
    httpd.running = 0;
    close(httpd.listen_fd);
}
