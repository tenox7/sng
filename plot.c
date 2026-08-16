#define _GNU_SOURCE
#include "compat.h"
#include "plot.h"
#include "threading.h"
#include "os/os_interface.h"
#include "datasource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

typedef struct {
    double min_value;
    double max_value;
    double avg_value;
    double last_value;
    double min_value_secondary;
    double max_value_secondary;
    double avg_value_secondary;
    double last_value_secondary;
} plot_stats_t;

static plot_stats_t plot_stats_cache[32];
static char system_hostname[256] = "";

/* Mix fg over bg.  Used for the area under a trace, which is the accent color
 * knocked back so the trace itself stays the brightest thing in the panel.
 * Two colors per plot instead of a stipple pattern, which the renderer API
 * does not have. */
static color_t blend_color(color_t fg, color_t bg, int32_t pct) {
    color_t c;
    c.r = (uint8_t)((fg.r * pct + bg.r * (100 - pct)) / 100);
    c.g = (uint8_t)((fg.g * pct + bg.g * (100 - pct)) / 100);
    c.b = (uint8_t)((fg.b * pct + bg.b * (100 - pct)) / 100);
    c.a = 255;
    return c;
}

/* Round a scale maximum up to a readable number, so the axis says 400ms
 * instead of 319.9ms. */
static double nice_max(double v) {
    static const double steps[] = {
        1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0
    };
    double scale = 1.0, n;
    int i;

    if (v <= 0.0) return 1.0;

    while (v / scale >= 10.0) scale *= 10.0;
    while (v / scale < 1.0) scale /= 10.0;

    n = v / scale;
    for (i = 0; i < (int)(sizeof(steps) / sizeof(steps[0])); i++) {
        if (n <= steps[i]) return steps[i] * scale;
    }
    return 10.0 * scale;
}

static void format_value(plot_t *plot, double value, char *buf, size_t size) {
    const char *unit;

    if (plot->data_source && plot->data_source->datasource &&
        plot->data_source->datasource->handler->format_value) {
        plot->data_source->datasource->handler->format_value(value, buf, size);
        return;
    }

    unit = (plot->data_source && plot->data_source->datasource) ?
           datasource_get_unit(plot->data_source->datasource) : "";
    snprintf(buf, size, "%.1f%s", value, unit);
}

static void calculate_stats(plot_t *plot, data_source_t *data_source, uint32_t plot_index) {
    if (!plot) return;

    if (data_source && data_source->datasource && data_source->datasource->handler->get_stats) {
        datasource_stats_t ds_stats;
        if (data_source->datasource->handler->get_stats(data_source->datasource->context, &ds_stats) == 1) {
            plot_stats_cache[plot_index].min_value = ds_stats.min;
            plot_stats_cache[plot_index].max_value = ds_stats.max;
            plot_stats_cache[plot_index].avg_value = ds_stats.avg;
            plot_stats_cache[plot_index].last_value = ds_stats.last;
            plot_stats_cache[plot_index].min_value_secondary = ds_stats.min_secondary;
            plot_stats_cache[plot_index].max_value_secondary = ds_stats.max_secondary;
            plot_stats_cache[plot_index].avg_value_secondary = ds_stats.avg_secondary;
            plot_stats_cache[plot_index].last_value_secondary = ds_stats.last_secondary;
        }
    }
}



void plot_draw(plot_t *plot, renderer_t *renderer, font_t *font,
               int32_t x, int32_t y, int32_t width, int32_t height, config_t *global_config, uint32_t plot_index,
               int32_t hover_x, int32_t hover_y) {
    color_t border_color, line_color, fill_color;
    char title[256];
    int32_t plot_y, plot_height, footer_y;
    rect_t border_rect;
    char stats_text[128];
    double fixed_max_scale, max_val;
    char scale_text[64];
    const char* unit;
    int32_t scale_text_width, scale_text_height;
    int32_t scale_x;
    double temp_buffer[2048];
    double temp_buffer_secondary[2048];
    uint32_t temp_timestamps[2048];
    uint32_t data_count, head_pos, tail_pos;
    uint32_t data_count_secondary, head_pos_secondary, tail_pos_secondary;
    int32_t prev_out_x, prev_out_y;
    uint32_t i;
    double in_value, out_value;
    int32_t plot_x, plot_bottom, plot_top;
    int32_t in_bar_height, out_bar_height, out_y;
    double value;
    int32_t bar_height;
    int32_t text_width, text_height;
    int32_t text_x;
    uint32_t buffer_size;
    int32_t refresh_interval;
    uint32_t total_time_ms;
    char time_span_text[64];
    uint32_t minutes, hours, days;
    char *local_pos;
    char temp[256];
    size_t prefix_len;
    double max_primary, max_secondary;
    char formatted[64];
    char avg_formatted[64];
    char last_formatted[64];
    uint32_t now_ms;
    int32_t pixel_offset;
    int32_t plot_max_offset;
    uint32_t dual_count;
    int32_t line_h, grid_i, grid_y;
    int32_t prev_x, prev_y, swatch;
    int retro;

    if (!plot || !renderer || !font) return;

    calculate_stats(plot, plot->data_source, plot_index);

    retro = global_config->retro;
    border_color = global_config->border_color;
    line_color = plot->config->line_color;
    /* Knock the area back further for pale accents, otherwise a plot that sits
     * near its ceiling becomes a solid block with the trace lost inside it. */
    grid_i = (line_color.r * 30 + line_color.g * 59 + line_color.b * 11) / 100;
    fill_color = blend_color(line_color, global_config->panel_color,
                             32 - (grid_i * 16) / 255);

    font_get_text_size(font, "0", &text_width, &line_h);
    if (line_h < 6) line_h = 6;

    snprintf(title, sizeof(title), "%s", plot->config->name);
    if (strstr(title, "local")) {
        local_pos = strstr(title, "local");
        prefix_len = local_pos - title;
        strncpy(temp, title, prefix_len);
        temp[prefix_len] = '\0';
        strcat(temp, system_hostname);
        strcat(temp, local_pos + 5);
        snprintf(title, sizeof(title), "%s", temp);
    }
    font_draw_text(renderer, font, global_config->text_color, x, y + 1, title);

    plot_y = y + line_h + 4;
    footer_y = y + height - line_h - 1;
    plot_height = footer_y - plot_y - 3;
    if (plot_height < 8) plot_height = 8;
    plot_top = plot_y + 2;
    plot_bottom = plot_y + plot_height - 2;

    border_rect.x = x;
    border_rect.y = plot_y;
    border_rect.w = width;
    border_rect.h = plot_height;
    if (!retro) {
        renderer_set_color(renderer, global_config->panel_color);
        renderer_fill_rect(renderer, border_rect);

        /* Horizontal guides at the quarters, so a trace can be read off the
         * panel without hovering it. */
        renderer_set_color(renderer, global_config->grid_color);
        for (grid_i = 1; grid_i < 4; grid_i++) {
            grid_y = plot_y + (plot_height * grid_i) / 4;
            renderer_draw_line(renderer, x + 1, grid_y, x + width - 2, grid_y);
        }
    }

    renderer_set_color(renderer, border_color);
    renderer_draw_rect(renderer, border_rect);

    if (!plot->data_buffer || ringbuf_count(plot->data_buffer) == 0) {
        snprintf(stats_text, sizeof(stats_text), "No data");
        font_draw_text(renderer, font, global_config->text_dim_color, x, footer_y, stats_text);
        return;
    }

    if (plot->data_source && plot->data_source->datasource) {
        fixed_max_scale = datasource_get_max_scale(plot->data_source->datasource);
        unit = datasource_get_unit(plot->data_source->datasource);
    } else {
        fixed_max_scale = 0.0;
        unit = "";
    }

    if (!ringbuf_read_snapshot(plot->data_buffer, temp_buffer, temp_timestamps, 2048, &data_count, &head_pos, &tail_pos))
        return;

    if (plot->is_dual && plot->data_buffer_secondary) {
        if (!ringbuf_read_snapshot(plot->data_buffer_secondary, temp_buffer_secondary, NULL, 2048,
                                  &data_count_secondary, &head_pos_secondary, &tail_pos_secondary))
            return;
    }

    now_ms = os_get_time_ms();
    refresh_interval = (plot->config->refresh_interval_ms > 0) ?
                      plot->config->refresh_interval_ms :
                      global_config->refresh_interval_ms;
    plot_max_offset = width - 3;
    if (plot_max_offset < 0) plot_max_offset = 0;

    if (fixed_max_scale > 0.0) {
        max_val = fixed_max_scale;
    } else {
        max_val = 0.0;
        for (i = 0; i < data_count; i++) {
            if (temp_buffer[i] > max_val)
                max_val = temp_buffer[i];
        }
        if (plot->is_dual) {
            for (i = 0; i < data_count_secondary; i++) {
                if (temp_buffer_secondary[i] > max_val)
                    max_val = temp_buffer_secondary[i];
            }
        }
        if (max_val <= 0.0)
            max_val = 1.0;
        if (!retro)
            max_val = nice_max(max_val);
    }

    if (plot->data_source && plot->data_source->datasource && plot->data_source->datasource->handler->format_value) {
        plot->data_source->datasource->handler->format_value(max_val, formatted, sizeof(formatted));
        snprintf(scale_text, sizeof(scale_text), "%s", formatted);
    } else {
        if (strlen(unit) > 0) {
            snprintf(scale_text, sizeof(scale_text), "%.1f%s", max_val, unit);
        } else {
            snprintf(scale_text, sizeof(scale_text), "%.1f", max_val);
        }
    }

    font_get_text_size(font, scale_text, &scale_text_width, &scale_text_height);
    scale_x = x + width - scale_text_width;
    font_draw_text(renderer, font,
                   retro ? global_config->text_color : global_config->text_dim_color,
                   scale_x, y + 1, scale_text);

    dual_count = (plot->is_dual && plot->data_buffer_secondary) ?
                 ((data_count < data_count_secondary) ? data_count : data_count_secondary) :
                 data_count;

    /* Three passes over the samples, one color each, rather than switching the
     * GC foreground per column - that matters over a slow X connection. */

    renderer_set_color(renderer, global_config->error_line_color);
    for (i = 0; i < dual_count; i++) {
        if (temp_buffer[i] >= 0 &&
            !(plot->is_dual && plot->data_buffer_secondary && temp_buffer_secondary[i] < 0))
            continue;
        pixel_offset = (int32_t)((now_ms - temp_timestamps[i]) / (uint32_t)refresh_interval);
        if (pixel_offset < 0 || pixel_offset > plot_max_offset) continue;
        plot_x = x + width - 2 - pixel_offset;
        renderer_draw_line(renderer, plot_x, plot_top, plot_x, plot_bottom);
    }

    /* Retro fills the whole column in the one line color; otherwise the body is
     * the knocked-back fill and only the top edge carries the trace. */
    renderer_set_color(renderer, retro ? line_color : fill_color);
    for (i = 0; i < dual_count; i++) {
        value = temp_buffer[i];
        if (value < 0) continue;
        pixel_offset = (int32_t)((now_ms - temp_timestamps[i]) / (uint32_t)refresh_interval);
        if (pixel_offset < 0 || pixel_offset > plot_max_offset) continue;
        plot_x = x + width - 2 - pixel_offset;

        bar_height = (int32_t)((value / max_val) * (plot_height - 4));
        if (bar_height < 1) bar_height = 1;
        if (bar_height > plot_height - 4) bar_height = plot_height - 4;
        renderer_draw_line(renderer, plot_x, plot_bottom - bar_height + (retro ? 0 : 1),
                           plot_x, plot_bottom);
    }

    if (!retro) {
        renderer_set_color(renderer, line_color);
        prev_x = prev_y = -1;
        for (i = 0; i < dual_count; i++) {
            value = temp_buffer[i];
            pixel_offset = (int32_t)((now_ms - temp_timestamps[i]) / (uint32_t)refresh_interval);
            if (value < 0 || pixel_offset < 0 || pixel_offset > plot_max_offset) {
                prev_x = prev_y = -1;
                continue;
            }
            plot_x = x + width - 2 - pixel_offset;

            bar_height = (int32_t)((value / max_val) * (plot_height - 4));
            if (bar_height < 1) bar_height = 1;
            if (bar_height > plot_height - 4) bar_height = plot_height - 4;
            in_bar_height = plot_bottom - bar_height;

            if (prev_x >= 0)
                renderer_draw_line(renderer, prev_x, prev_y, plot_x, in_bar_height);
            else
                renderer_draw_line(renderer, plot_x, in_bar_height, plot_x, in_bar_height);

            prev_x = plot_x;
            prev_y = in_bar_height;
        }
    }

    if (plot->is_dual && plot->data_buffer_secondary) {
        renderer_set_color(renderer, plot->config->line_color_secondary);
        prev_out_x = prev_out_y = -1;
        for (i = 0; i < dual_count; i++) {
            in_value = temp_buffer[i];
            out_value = temp_buffer_secondary[i];
            pixel_offset = (int32_t)((now_ms - temp_timestamps[i]) / (uint32_t)refresh_interval);
            if (in_value < 0 || out_value < 0 ||
                pixel_offset < 0 || pixel_offset > plot_max_offset) {
                prev_out_x = prev_out_y = -1;
                continue;
            }
            plot_x = x + width - 2 - pixel_offset;

            out_bar_height = (int32_t)((out_value / max_val) * (plot_height - 4));
            if (out_bar_height > plot_height - 4) out_bar_height = plot_height - 4;
            out_y = plot_bottom - out_bar_height;

            if (prev_out_x >= 0)
                renderer_draw_line(renderer, prev_out_x, prev_out_y, plot_x, out_y);
            else
                renderer_draw_line(renderer, plot_x, out_y, plot_x, out_y);

            prev_out_x = plot_x;
            prev_out_y = out_y;
        }
    }


    format_value(plot, plot_stats_cache[plot_index].avg_value, avg_formatted, sizeof(avg_formatted));
    format_value(plot, plot_stats_cache[plot_index].last_value, last_formatted, sizeof(last_formatted));

    swatch = 0;
    if (!retro && plot->is_dual && plot->data_buffer_secondary) {
        swatch = line_h - 6;
        if (swatch < 3) swatch = 3;
    }

    /* Dual plots read as a legend: a swatch in front of each series value, so
     * which color is which is obvious without a key elsewhere. */
    if (swatch) {
        format_value(plot, plot_stats_cache[plot_index].last_value_secondary,
                     formatted, sizeof(formatted));
        font_get_text_size(font, last_formatted, &text_width, &text_height);
        font_get_text_size(font, formatted, &scale_text_width, &scale_text_height);
        text_x = x + width - (2 * swatch + text_width + scale_text_width + 16);
    }

    if (swatch && text_x > x) {
        border_rect.y = footer_y + 3;
        border_rect.w = swatch;
        border_rect.h = swatch;

        border_rect.x = text_x;
        renderer_set_color(renderer, line_color);
        renderer_fill_rect(renderer, border_rect);
        font_draw_text(renderer, font, line_color, text_x + swatch + 4, footer_y,
                       last_formatted);

        border_rect.x = text_x + swatch + 4 + text_width + 8;
        renderer_set_color(renderer, plot->config->line_color_secondary);
        renderer_fill_rect(renderer, border_rect);
        font_draw_text(renderer, font, plot->config->line_color_secondary,
                       border_rect.x + swatch + 4, footer_y, formatted);
    } else {
        /* Too narrow to split, fall back to the combined reading. */
        swatch = 0;
        if (plot->is_dual && plot->data_source && plot->data_source->datasource &&
            plot->data_source->datasource->handler->format_dual_stats) {
            plot->data_source->datasource->handler->format_dual_stats(
                plot_stats_cache[plot_index].last_value,
                plot_stats_cache[plot_index].last_value_secondary,
                stats_text, sizeof(stats_text));
        } else {
            snprintf(stats_text, sizeof(stats_text), "%s", last_formatted);
        }

        font_get_text_size(font, stats_text, &text_width, &text_height);
        text_x = x + width - text_width;
        font_draw_text(renderer, font, retro ? global_config->text_color : line_color,
                       text_x, footer_y, stats_text);
    }

    buffer_size = plot->data_buffer->size;
    total_time_ms = buffer_size * (uint32_t)refresh_interval;
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

    font_draw_text(renderer, font,
                   retro ? global_config->text_color : global_config->text_dim_color,
                   x, footer_y, time_span_text);

    /* The average has been computed all along and never shown; it fits in the
     * gap between the time span and the current reading. */
    if (!retro) {
        snprintf(temp, sizeof(temp), "avg %s", avg_formatted);
        font_get_text_size(font, temp, &scale_text_width, &scale_text_height);
        font_get_text_size(font, time_span_text, &text_width, &text_height);
        scale_x = x + (width - scale_text_width) / 2;
        if (scale_x > x + text_width + 8 && scale_x + scale_text_width < text_x - 8)
            font_draw_text(renderer, font, global_config->text_dim_color, scale_x,
                           footer_y, temp);
    }

    if (hover_x >= x && hover_x < x + width && hover_y >= plot_y && hover_y < plot_y + plot_height) {
        int32_t guide_x, guide_top, guide_bottom;
        uint32_t data_index;
        int32_t sample_pixel_offset;
        int32_t sample_plot_x;
        int32_t distance;
        int32_t best_distance;
        int hover_found;
        double hover_value;
        char hover_text[128];
        int32_t hover_text_width, hover_text_height;
        int32_t hover_text_x, hover_text_y;
        uint32_t time_offset_ms;
        uint32_t time_seconds, time_minutes, time_hours;
        char time_text[64];
        char value_text[64];

        guide_x = hover_x;
        guide_top = plot_top;
        guide_bottom = plot_bottom;

        renderer_set_color(renderer, retro ? border_color : global_config->text_dim_color);
        renderer_draw_line(renderer, guide_x, guide_top, guide_x, guide_bottom);

        hover_found = 0;
        best_distance = 3;
        data_index = 0;
        for (i = 0; i < dual_count; i++) {
            sample_pixel_offset = (int32_t)((now_ms - temp_timestamps[i]) / (uint32_t)refresh_interval);
            if (sample_pixel_offset < 0 || sample_pixel_offset > plot_max_offset) continue;
            sample_plot_x = x + width - 2 - sample_pixel_offset;
            distance = sample_plot_x - hover_x;
            if (distance < 0) distance = -distance;
            if (distance <= best_distance) {
                best_distance = distance;
                data_index = i;
                hover_found = 1;
            }
        }

        if (hover_found) {
            double hover_value_secondary;
            hover_value = temp_buffer[data_index];
            hover_value_secondary = (plot->is_dual && data_index < data_count_secondary) ? temp_buffer_secondary[data_index] : 0.0;

            time_offset_ms = now_ms - temp_timestamps[data_index];
            time_seconds = time_offset_ms / 1000;
            time_minutes = time_seconds / 60;
            time_hours = time_minutes / 60;

            if (time_seconds < 60) {
                snprintf(time_text, sizeof(time_text), "%us ago", time_seconds);
            } else if (time_minutes < 10) {
                snprintf(time_text, sizeof(time_text), "%um%us ago", time_minutes, time_seconds % 60);
            } else if (time_minutes < 60) {
                snprintf(time_text, sizeof(time_text), "%um ago", time_minutes);
            } else {
                snprintf(time_text, sizeof(time_text), "%uh%um ago", time_hours, time_minutes % 60);
            }

            if (plot->is_dual && plot->data_source && plot->data_source->datasource && plot->data_source->datasource->handler->format_dual_stats) {
                plot->data_source->datasource->handler->format_dual_stats(hover_value, hover_value_secondary, value_text, sizeof(value_text));
            } else if (plot->data_source && plot->data_source->datasource && plot->data_source->datasource->handler->format_value) {
                plot->data_source->datasource->handler->format_value(hover_value, value_text, sizeof(value_text));
            } else {
                if (strlen(unit) > 0) {
                    snprintf(value_text, sizeof(value_text), "%.1f%s", hover_value, unit);
                } else {
                    snprintf(value_text, sizeof(value_text), "%.1f", hover_value);
                }
            }

            snprintf(hover_text, sizeof(hover_text), "%s - %s", value_text, time_text);
            font_get_text_size(font, hover_text, &hover_text_width, &hover_text_height);

            hover_text_x = guide_x + 6;
            if (hover_text_x + hover_text_width + 4 > x + width) {
                hover_text_x = guide_x - hover_text_width - 6;
            }
            if (hover_text_x < x + 2) {
                hover_text_x = x + 2;
            }

            hover_text_y = hover_y - hover_text_height - 5;
            if (hover_text_y < plot_top) {
                hover_text_y = hover_y + 5;
            }
            if (hover_text_y + hover_text_height > plot_bottom) {
                hover_text_y = plot_bottom - hover_text_height;
            }

            /* Opaque backing, otherwise the readout sits on top of the trace
             * and neither can be read. */
            if (!retro) {
                border_rect.x = hover_text_x - 3;
                border_rect.y = hover_text_y - 1;
                border_rect.w = hover_text_width + 6;
                border_rect.h = hover_text_height + 2;
                renderer_set_color(renderer, global_config->background_color);
                renderer_fill_rect(renderer, border_rect);
                renderer_set_color(renderer, global_config->grid_color);
                renderer_draw_rect(renderer, border_rect);
            }

            font_draw_text(renderer, font,
                           retro ? border_color : global_config->text_color,
                           hover_text_x, hover_text_y, hover_text);
        }
    }
}

plot_system_t *plot_system_create(config_t *config) {
    plot_system_t *system;
    int32_t plot_spacing;
    int32_t window_height;
    char window_title[300];
    int should_be_fullscreen;
    uint32_t i;

    if (!config) return NULL;
    
    system = malloc(sizeof(plot_system_t));
    if (!system) return NULL;
    
    system->config = config;
    system->plot_count = config->plot_count;
    system->plots = malloc(sizeof(plot_t) * system->plot_count);
    if (!system->plots) {
        free(system);
        return NULL;
    }
    
    plot_spacing = 10;
    window_height = config->plot_count * (config->default_height + plot_spacing) + config->window_margin * 2;
    if (gethostname(system_hostname, sizeof(system_hostname)) == 0) {
        char *dot = strchr(system_hostname, '.');
        if (dot) *dot = '\0';
        snprintf(window_title, sizeof(window_title), "SNG : %s", system_hostname);
    } else {
        strcpy(window_title, "SNG");
        strcpy(system_hostname, "localhost");
    }

    system->window = window_create(window_title,
                                  config->default_width,
                                  window_height);
    if (!system->window) {
        free(system->plots);
        free(system);
        return NULL;
    }

    should_be_fullscreen = (config->fullscreen == FULLSCREEN_ON || config->fullscreen == FULLSCREEN_FORCE);
    window_set_fullscreen(system->window, should_be_fullscreen);

    if (should_be_fullscreen) {
        window_set_topmost(system->window, 1);
    }

    system->renderer = renderer_create(system->window);
    if (!system->renderer) {
        window_destroy(system->window);
        free(system->plots);
        free(system);
        return NULL;
    }
    
#ifdef GFX_X11
    if (config->font_name) {
        system->font = font_create(config->font_name, 0);
    } else {
        int32_t font_size = (int32_t)(11.0f * config->font_size);
        if (font_size < 1) font_size = 1;
        system->font = font_create("", font_size);
    }
#else
    {
        int32_t font_size = (int32_t)(11.0f * config->font_size);
        if (font_size < 1) font_size = 1;
        system->font = font_create("", font_size);
    }
#endif
    if (!system->font) {
        renderer_destroy(system->renderer);
        window_destroy(system->window);
        free(system->plots);
        free(system);
        return NULL;
    }
    
    system->fullscreen = should_be_fullscreen;
    system->last_plot_width = 0;

    system->cached_window_width = 0;
    system->cached_window_height = 0;
    system->window_size_dirty = 1;

    system->needs_redraw = 1;

    system->last_fullscreen_check_ms = os_get_time_ms();

    system->mouse_x = -1;
    system->mouse_y = -1;

    for (i = 0; i < system->plot_count; i++) {
        plot_t *plot = &system->plots[i];
        plot->config = &config->plots[i];
        plot->data_buffer = NULL;
        plot_stats_cache[i].min_value = 0.0;
        plot_stats_cache[i].max_value = 0.0;
        plot_stats_cache[i].avg_value = 0.0;
        plot_stats_cache[i].last_value = 0.0;
        plot->active = 1;

        plot->cached_data_count = 0;
        plot->cached_data_count_secondary = 0;
        plot->cached_head_position = 0;
        plot->cached_head_position_secondary = 0;
        plot->stats_dirty = 1;
    }
    
    return system;
}

void plot_system_destroy(plot_system_t *system) {
    if (!system) return;
    
    font_destroy(system->font);
    renderer_destroy(system->renderer);
    window_destroy(system->window);
    free(system->plots);
    free(system);
}

void plot_system_connect_data_buffers(plot_system_t *system, data_collector_t *collector) {
    uint32_t i;
    if (!system || !collector) return;

    for (i = 0; i < system->plot_count && i < collector->source_count; i++) {
        system->plots[i].data_buffer = collector->sources[i].data_buffer;
        system->plots[i].data_buffer_secondary = collector->sources[i].data_buffer_secondary;
        system->plots[i].data_source = &collector->sources[i];
        system->plots[i].is_dual = collector->sources[i].is_dual;
    }
}

static int plot_system_needs_redraw(plot_system_t *system) {
    uint32_t i;

    if (!system) return 0;

    if (window_was_resized() || system->window_size_dirty || system->needs_redraw) {
        return 1;
    }
    for (i = 0; i < system->plot_count; i++) {
        plot_t *plot = &system->plots[i];
        if (!plot->data_buffer) continue;

        if (plot->cached_data_count != plot->data_buffer->count ||
            plot->cached_head_position != plot->data_buffer->head) {
            return 1;
        }

        if (plot->is_dual && plot->data_buffer_secondary &&
            (plot->cached_data_count_secondary != plot->data_buffer_secondary->count ||
             plot->cached_head_position_secondary != plot->data_buffer_secondary->head)) {
            return 1;
        }
    }

    return 0;
}

int plot_system_update(plot_system_t *system) {
    graphics_event_t event;
    int window_resized;
    int needs_full_render;
    int32_t current_plot_width;
    int32_t plot_height;
    int32_t plot_spacing;
    int32_t margin;
    uint32_t i;

    if (!system) return 0;

    if (!graphics_wait_events()) {
        return 0;
    }
    while (graphics_get_event(&event)) {
        switch (event.type) {
            case GRAPHICS_EVENT_QUIT:
                return 0;
            case GRAPHICS_EVENT_REFRESH:
                system->needs_redraw = 1;
                break;
            case GRAPHICS_EVENT_FULLSCREEN_TOGGLE:
                window_set_fullscreen(system->window, !system->fullscreen);
                system->fullscreen = !system->fullscreen;
                if (system->fullscreen) {
                    window_set_topmost(system->window, 1);
                } else {
                    window_set_topmost(system->window, 0);
                }
                system->needs_redraw = 1;
                break;
            case GRAPHICS_EVENT_MOUSE_MOTION:
                system->mouse_x = event.mouse_x;
                system->mouse_y = event.mouse_y;
                system->needs_redraw = 1;
                break;
            case GRAPHICS_EVENT_NONE:
            case GRAPHICS_EVENT_KEY_PRESS:
            default:
                break;
        }
    }

    if (system->config->fullscreen == FULLSCREEN_FORCE) {
        uint64_t current_time;
        current_time = os_get_time_ms();
        if (current_time - system->last_fullscreen_check_ms >= (uint64_t)system->config->refresh_interval_ms) {
            int currently_fullscreen;
            currently_fullscreen = window_is_fullscreen(system->window);
            if (!currently_fullscreen) {
                window_set_fullscreen(system->window, 1);
                window_set_topmost(system->window, 1);
                system->fullscreen = 1;
                system->needs_redraw = 1;
            }
            system->last_fullscreen_check_ms = current_time;
        }
    }

    window_resized = window_was_resized();
    needs_full_render = 0;

    if (window_resized || system->window_size_dirty) {
        window_get_size(system->window, &system->cached_window_width, &system->cached_window_height);
        system->window_size_dirty = 0;
        system->needs_redraw = 1;
        needs_full_render = 1;
    }

    current_plot_width = system->cached_window_width - (system->config->window_margin * 2);
    if (system->last_plot_width != current_plot_width) {
        uint32_t new_buffer_size;
        new_buffer_size = current_plot_width - 2;
        if (new_buffer_size > 0) {
            for (i = 0; i < system->plot_count; i++) {
                if (system->plots[i].data_buffer) {
                    ringbuf_resize(system->plots[i].data_buffer, new_buffer_size);
                }
                if (system->plots[i].data_buffer_secondary) {
                    ringbuf_resize(system->plots[i].data_buffer_secondary, new_buffer_size);
                }
            }
        }
        system->last_plot_width = current_plot_width;
        system->needs_redraw = 1;
        needs_full_render = 1;
    }

    if (!plot_system_needs_redraw(system) && !needs_full_render) {
        return 1;
    }

    plot_height = system->config->default_height;
    margin = system->config->window_margin;
    plot_spacing = 10;

    renderer_clear(system->renderer, system->config->background_color);
    for (i = 0; i < system->plot_count; i++) {
        int32_t y;
        int32_t hover_x, hover_y;
        y = i * (plot_height + plot_spacing) + margin;

        hover_x = -1;
        hover_y = -1;
        if (system->mouse_x >= margin && system->mouse_x < margin + current_plot_width &&
            system->mouse_y >= y && system->mouse_y < y + plot_height) {
            hover_x = system->mouse_x;
            hover_y = system->mouse_y;
        }

        plot_draw(&system->plots[i], system->renderer, system->font,
                  margin, y, current_plot_width, plot_height, system->config, i, hover_x, hover_y);
    }

    graphics_draw_fps_counter(system->renderer, system->font, system->config->fps_counter);

    renderer_present(system->renderer);

    system->needs_redraw = 0;

    return 1;
}
