#include "../datasource.h"
#include "../os/os_interface.h"
#include <stddef.h>
#include <stdlib.h>
#include "../compat.h"
#include <stdio.h>

typedef struct {
    double min;
    double max;
    uint64_t sum;
    uint32_t sample_count;
    double last;
} loadavg_stats_t;

static int loadavg_init(const char *target, void **context) {
    loadavg_stats_t *ctx = malloc(sizeof(loadavg_stats_t));
    if (!ctx) return 0;

    ctx->min = 1000.0;
    ctx->max = 0.0;
    ctx->sum = 0;
    ctx->sample_count = 0;
    ctx->last = 0.0;

    *context = ctx;
    return 1;
}

static int loadavg_collect(void *context, double *value) {
    loadavg_stats_t *ctx;

    ctx = (loadavg_stats_t *)context;
    if (!ctx || !value) return 0;

    if (!os_loadavg_get_stats(value)) {
        return 0;
    }

    if (*value < ctx->min) ctx->min = *value;
    if (*value > ctx->max) ctx->max = *value;
    ctx->sum += (uint64_t)*value;
    ctx->last = *value;
    ctx->sample_count++;

    return 1;
}

static int loadavg_get_stats(void *context, datasource_stats_t *stats) {
    loadavg_stats_t *ctx = (loadavg_stats_t *)context;
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

    stats->min = ctx->min;
    stats->max = ctx->max;
    stats->avg = (double)(ctx->sum / ctx->sample_count);
    stats->last = ctx->last;
    stats->min_secondary = 0.0;
    stats->max_secondary = 0.0;
    stats->avg_secondary = 0.0;
    stats->last_secondary = 0.0;

    return 1;
}

static void loadavg_cleanup(void *context) {
    free(context);
}

static void loadavg_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f", value);
}

datasource_handler_t loadavg_handler = {
    .init = loadavg_init,
    .collect = loadavg_collect,
    .collect_dual = NULL,
    .get_stats = loadavg_get_stats,
    .format_value = loadavg_format_value,
    .cleanup = loadavg_cleanup,
    .name = "loadavg",
    .unit = "",
    .is_dual = 0,
    .max_scale = 0.0
};
