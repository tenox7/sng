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
#include <unistd.h>

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
    color_t border_color;
    char title[256];
    int32_t plot_y, plot_height;
    rect_t border_rect;
    char stats_text[128];
    double fixed_max_scale, max_val;
    char scale_text[64];
    const char* unit;
    int32_t scale_text_width, scale_text_height;
    int32_t scale_x;
    double temp_buffer[2048];
    double temp_buffer_secondary[2048];
    uint32_t data_count, head_pos, tail_pos;
    uint32_t data_count_secondary, head_pos_secondary, tail_pos_secondary;
    int32_t prev_out_x, prev_out_y;
    uint32_t i;
    double in_value, out_value;
    int32_t plot_x, plot_bottom;
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

    if (!plot || !renderer || !font) return;
    
    calculate_stats(plot, plot->data_source, plot_index);
    
    border_color = global_config->border_color;
    renderer_set_color(renderer, border_color);

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
    font_draw_text(renderer, font, global_config->text_color, x, y + 5, title);
    
    plot_y = y + 20;
    plot_height = height - 40;
    border_rect.x = x;
    border_rect.y = plot_y;
    border_rect.w = width;
    border_rect.h = plot_height;
    renderer_draw_rect(renderer, border_rect);
    
    if (!plot->data_buffer || ringbuf_count(plot->data_buffer) == 0) {
        snprintf(stats_text, sizeof(stats_text), "No data");
        font_draw_text(renderer, font, global_config->text_color, x, y + height - 15, stats_text);
        return;
    }

    if (plot->data_source && plot->data_source->datasource) {
        fixed_max_scale = datasource_get_max_scale(plot->data_source->datasource);
        unit = datasource_get_unit(plot->data_source->datasource);
    } else {
        fixed_max_scale = 0.0;
        unit = "";
    }

    if (!ringbuf_read_snapshot(plot->data_buffer, temp_buffer, 2048, &data_count, &head_pos, &tail_pos))
        return;

    if (plot->is_dual && plot->data_buffer_secondary) {
        if (!ringbuf_read_snapshot(plot->data_buffer_secondary, temp_buffer_secondary, 2048,
                                  &data_count_secondary, &head_pos_secondary, &tail_pos_secondary))
            return;
    }

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
    font_draw_text(renderer, font, global_config->text_color, scale_x, y + 5, scale_text);

    if (plot->is_dual && plot->data_buffer_secondary) {
        prev_out_x = -1;
        prev_out_y = -1;

        for (i = 0; i < data_count; i++) {
            in_value = temp_buffer[i];
            out_value = temp_buffer_secondary[i];

            plot_x = x + width - 2 - (data_count - 1 - i);
            plot_bottom = plot_y + plot_height - 2;

            if (in_value < 0 || out_value < 0) {
                renderer_set_color(renderer, global_config->error_line_color);
                renderer_draw_line(renderer, plot_x, plot_y + 2, plot_x, plot_bottom);
                prev_out_x = prev_out_y = -1;
            } else {
                in_bar_height = (int32_t)((in_value / max_val) * (plot_height - 4));
                if (in_bar_height < 1) in_bar_height = 1;

                renderer_set_color(renderer, plot->config->line_color);
                renderer_draw_line(renderer, plot_x, plot_bottom - in_bar_height, plot_x, plot_bottom);

                out_bar_height = (int32_t)((out_value / max_val) * (plot_height - 4));
                out_y = plot_bottom - out_bar_height;

                renderer_set_color(renderer, plot->config->line_color_secondary);

                if (prev_out_x >= 0 && prev_out_y >= 0) {
                    renderer_draw_line(renderer, prev_out_x, prev_out_y, plot_x, out_y);
                } else {
                    renderer_draw_line(renderer, plot_x, out_y, plot_x, out_y);
                }

                prev_out_x = plot_x;
                prev_out_y = out_y;
            }
        }
    } else {
        for (i = 0; i < data_count; i++) {
            value = temp_buffer[i];
            plot_x = x + width - 2 - (data_count - 1 - i);
            plot_bottom = plot_y + plot_height - 2;

            if (value < 0) {
                renderer_set_color(renderer, global_config->error_line_color);
                renderer_draw_line(renderer, plot_x, plot_y + 2, plot_x, plot_bottom);
            } else {
                bar_height = (int32_t)((value / max_val) * (plot_height - 4));
                if (bar_height < 1) bar_height = 1;

                renderer_set_color(renderer, plot->config->line_color);
                renderer_draw_line(renderer, plot_x, plot_bottom - bar_height, plot_x, plot_bottom);
            }
        }
    }
    
    if (plot->data_source && plot->data_source->datasource && plot->data_source->datasource->handler->format_value) {
        plot->data_source->datasource->handler->format_value(plot_stats_cache[plot_index].avg_value, avg_formatted, sizeof(avg_formatted));
        plot->data_source->datasource->handler->format_value(plot_stats_cache[plot_index].last_value, last_formatted, sizeof(last_formatted));
        if (plot->is_dual && plot->data_source->datasource->handler->format_dual_stats) {
            plot->data_source->datasource->handler->format_dual_stats(
                plot_stats_cache[plot_index].last_value,
                plot_stats_cache[plot_index].last_value_secondary,
                stats_text, sizeof(stats_text));
        } else {
            snprintf(stats_text, sizeof(stats_text), "%s", last_formatted);
        }
    } else {
        if (plot->data_source && plot->data_source->datasource) {
            unit = datasource_get_unit(plot->data_source->datasource);
        } else {
            unit = "";
        }
        if (strlen(unit) > 0) {
            snprintf(stats_text, sizeof(stats_text), "%.1f%s",
                     plot_stats_cache[plot_index].last_value, unit);
        } else {
            snprintf(stats_text, sizeof(stats_text), "%.1f",
                     plot_stats_cache[plot_index].last_value);
        }
    }

    font_get_text_size(font, stats_text, &text_width, &text_height);
    text_x = x + width - text_width;
    font_draw_text(renderer, font, global_config->text_color, text_x, y + height - 15, stats_text);

    buffer_size = plot->data_buffer->size;
    refresh_interval = (plot->config->refresh_interval_ms > 0) ?
                      plot->config->refresh_interval_ms :
                      global_config->refresh_interval_ms;
    total_time_ms = buffer_size * refresh_interval;
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

    font_draw_text(renderer, font, global_config->text_color, x, y + height - 15, time_span_text);

    if (hover_x >= x && hover_x < x + width && hover_y >= plot_y && hover_y < plot_y + plot_height) {
        int32_t guide_x, guide_top, guide_bottom;
        uint32_t data_index;
        int32_t offset_from_right;
        double hover_value;
        char hover_text[128];
        int32_t hover_text_width, hover_text_height;
        int32_t hover_text_x, hover_text_y;
        uint32_t time_offset_ms;
        uint32_t time_seconds, time_minutes, time_hours;
        char time_text[64];
        char value_text[64];

        guide_x = hover_x;
        guide_top = plot_y + 2;
        guide_bottom = plot_y + plot_height - 2;

        renderer_set_color(renderer, border_color);
        renderer_draw_line(renderer, guide_x, guide_top, guide_x, guide_bottom);

        offset_from_right = (x + width - 2) - hover_x;
        if (offset_from_right >= 0 && offset_from_right < (int32_t)data_count) {
            data_index = data_count - 1 - offset_from_right;
            hover_value = temp_buffer[data_index];

            time_offset_ms = offset_from_right * refresh_interval;
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

            if (plot->data_source && plot->data_source->datasource && plot->data_source->datasource->handler->format_value) {
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

            hover_text_x = guide_x + 5;
            if (hover_text_x + hover_text_width > x + width) {
                hover_text_x = guide_x - hover_text_width - 5;
            }
            if (hover_text_x < x) {
                hover_text_x = x;
            }

            hover_text_y = hover_y - hover_text_height - 5;
            if (hover_text_y < plot_y) {
                hover_text_y = hover_y + 5;
            }

            font_draw_text(renderer, font, border_color, hover_text_x, hover_text_y, hover_text);
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
    int32_t font_size = (int32_t)(11.0f * config->font_size);
    if (font_size < 1) font_size = 1;
    system->font = font_create("", font_size);
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
