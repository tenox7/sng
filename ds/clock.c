#include "../datasource.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

typedef struct {
    int mode;
    double min_hours;
    double max_hours;
    double min_minutes;
    double max_minutes;
    uint64_t sum_hours;
    uint64_t sum_minutes;
    uint32_t sample_count;
    double last_hours;
    double last_minutes;
} clock_context_t;

static int clock_init(const char *target, void **context) {
    clock_context_t *ctx;

    ctx = malloc(sizeof(clock_context_t));
    if (!ctx) return 0;

    ctx->mode = 24;
    if (target && strcmp(target, "12") == 0) {
        ctx->mode = 12;
    }

    ctx->min_hours = (double)ctx->mode;
    ctx->max_hours = 0.0;
    ctx->min_minutes = 60.0;
    ctx->max_minutes = 0.0;
    ctx->sum_hours = 0;
    ctx->sum_minutes = 0;
    ctx->sample_count = 0;
    ctx->last_hours = 0.0;
    ctx->last_minutes = 0.0;

    *context = ctx;
    return 1;
}

static int clock_collect(void *context, double *value) {
    clock_context_t *ctx;
    time_t now;
    struct tm *tm_info;
    double hours, minutes, scaled_minutes;

    ctx = (clock_context_t *)context;
    if (!ctx || !value) return 0;

    now = time(NULL);
    tm_info = localtime(&now);
    if (!tm_info) return 0;

    hours = (double)tm_info->tm_hour;
    minutes = (double)tm_info->tm_min;

    if (ctx->mode == 12) {
        if (hours == 0) {
            hours = 12.0;
        } else if (hours > 12) {
            hours -= 12.0;
        }
        scaled_minutes = minutes * 12.0 / 60.0;
    } else {
        scaled_minutes = minutes * 24.0 / 60.0;
    }

    *value = hours + (minutes / 100.0);

    if (hours < ctx->min_hours) ctx->min_hours = hours;
    if (hours > ctx->max_hours) ctx->max_hours = hours;
    if (scaled_minutes < ctx->min_minutes) ctx->min_minutes = scaled_minutes;
    if (scaled_minutes > ctx->max_minutes) ctx->max_minutes = scaled_minutes;
    ctx->sum_hours += (uint64_t)hours;
    ctx->sum_minutes += (uint64_t)scaled_minutes;
    ctx->last_hours = hours;
    ctx->last_minutes = scaled_minutes;
    ctx->sample_count++;

    return 1;
}

static int clock_collect_dual(void *context, double *hours_value, double *minutes_value) {
    clock_context_t *ctx;
    time_t now;
    struct tm *tm_info;
    double hours, minutes, scaled_minutes;

    ctx = (clock_context_t *)context;
    if (!ctx || !hours_value || !minutes_value) return 0;

    now = time(NULL);
    tm_info = localtime(&now);
    if (!tm_info) return 0;

    hours = (double)tm_info->tm_hour;
    minutes = (double)tm_info->tm_min;

    if (ctx->mode == 12) {
        if (hours == 0) {
            hours = 12.0;
        } else if (hours > 12) {
            hours -= 12.0;
        }
        scaled_minutes = minutes * 12.0 / 60.0;
    } else {
        scaled_minutes = minutes * 24.0 / 60.0;
    }

    *hours_value = hours;
    *minutes_value = scaled_minutes;

    if (hours < ctx->min_hours) ctx->min_hours = hours;
    if (hours > ctx->max_hours) ctx->max_hours = hours;
    if (scaled_minutes < ctx->min_minutes) ctx->min_minutes = scaled_minutes;
    if (scaled_minutes > ctx->max_minutes) ctx->max_minutes = scaled_minutes;
    ctx->sum_hours += (uint64_t)hours;
    ctx->sum_minutes += (uint64_t)scaled_minutes;
    ctx->last_hours = hours;
    ctx->last_minutes = scaled_minutes;
    ctx->sample_count++;

    return 1;
}

static int clock_get_stats(void *context, datasource_stats_t *stats) {
    clock_context_t *ctx;
    time_t now;
    struct tm *tm_info;
    double last_display, hours;

    ctx = (clock_context_t *)context;
    if (!ctx || !stats) return 0;

    if (ctx->sample_count == 0) {
        stats->min = 0.0;
        stats->max = 0.0;
        stats->avg = 0.0;
        stats->last = 0.0;
        stats->min_secondary = 0.0;
        stats->max_secondary = 0.0;
        stats->avg_secondary = 0.0;
        stats->last_secondary = 0.0;
        return 1;
    }

    now = time(NULL);
    tm_info = localtime(&now);
    if (tm_info) {
        hours = (double)tm_info->tm_hour;
        if (ctx->mode == 12) {
            if (hours == 0) {
                hours = 12.0;
            } else if (hours > 12) {
                hours -= 12.0;
            }
        }
        last_display = hours + ((double)tm_info->tm_min / 100.0);
    } else {
        last_display = ctx->last_hours + (ctx->last_minutes * 60.0 / (double)ctx->mode / 100.0);
    }

    stats->min = ctx->min_hours;
    stats->max = ctx->max_hours;
    stats->avg = (double)(ctx->sum_hours / ctx->sample_count);
    stats->last = last_display;
    stats->min_secondary = ctx->min_minutes;
    stats->max_secondary = ctx->max_minutes;
    stats->avg_secondary = (double)(ctx->sum_minutes / ctx->sample_count);
    stats->last_secondary = ctx->last_minutes;

    return 1;
}

static void clock_cleanup(void *context) {
    free(context);
}

static void clock_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.2f", value);
}

static double clock_get_max_scale(void *context) {
    clock_context_t *ctx;

    ctx = (clock_context_t *)context;
    if (!ctx) return 24.0;

    return (double)ctx->mode;
}

datasource_handler_t clock_handler = {
    .init = clock_init,
    .collect = clock_collect,
    .collect_dual = clock_collect_dual,
    .get_stats = clock_get_stats,
    .format_value = clock_format_value,
    .get_max_scale = clock_get_max_scale,
    .cleanup = clock_cleanup,
    .name = "clock",
    .unit = "",
    .is_dual = 1,
    .max_scale = 24.0
};
