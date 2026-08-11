#ifdef __VMS
#include "datasource.h"
#include "os/os_interface.h"
#else
#include "../datasource.h"
#include "../os/os_interface.h"
#endif
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    double min_used;
    double max_used;
    double min_swap;
    double max_swap;
    double sum_used;
    double sum_swap;
    uint32_t sample_count;
    double last_used;
    double last_swap;
} memory_stats_t;

static int memory_init(const char *target, void **context) {
    memory_stats_t *ctx = malloc(sizeof(memory_stats_t));
    if (!ctx) return 0;

    ctx->min_used = 100.0;
    ctx->max_used = 0.0;
    ctx->min_swap = 100.0;
    ctx->max_swap = 0.0;
    ctx->sum_used = 0.0;
    ctx->sum_swap = 0.0;
    ctx->sample_count = 0;
    ctx->last_used = 0.0;
    ctx->last_swap = 0.0;

    *context = ctx;
    return 1;
}

static void memory_accumulate(memory_stats_t *ctx, double used, double swap) {
    if (used < ctx->min_used) ctx->min_used = used;
    if (used > ctx->max_used) ctx->max_used = used;
    if (swap < ctx->min_swap) ctx->min_swap = swap;
    if (swap > ctx->max_swap) ctx->max_swap = swap;
    ctx->sum_used += used;
    ctx->sum_swap += swap;
    ctx->last_used = used;
    ctx->last_swap = swap;
    ctx->sample_count++;
}

static int memory_collect(void *context, double *value) {
    memory_stats_t *ctx;
    double swap;

    ctx = (memory_stats_t *)context;
    if (!ctx || !value) return 0;

    if (!os_memory_get_stats_dual(value, &swap)) return 0;

    memory_accumulate(ctx, *value, swap);
    return 1;
}

static int memory_collect_dual(void *context, double *used_value, double *swap_value) {
    memory_stats_t *ctx;

    ctx = (memory_stats_t *)context;
    if (!ctx || !used_value || !swap_value) return 0;

    if (!os_memory_get_stats_dual(used_value, swap_value)) return 0;

    memory_accumulate(ctx, *used_value, *swap_value);
    return 1;
}

static int memory_get_stats(void *context, datasource_stats_t *stats) {
    memory_stats_t *ctx = (memory_stats_t *)context;
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

    stats->min = ctx->min_used;
    stats->max = ctx->max_used;
    stats->avg = ctx->sum_used / (double)ctx->sample_count;
    stats->last = ctx->last_used;
    stats->min_secondary = ctx->min_swap;
    stats->max_secondary = ctx->max_swap;
    stats->avg_secondary = ctx->sum_swap / (double)ctx->sample_count;
    stats->last_secondary = ctx->last_swap;

    return 1;
}

static void memory_cleanup(void *context) {
    free(context);
}

static void memory_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f%%", value);
}

static void memory_format_dual_stats(double used, double swap, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f%%/%.1f%%", used, swap);
}

datasource_handler_t memory_handler = {
    memory_init,
    memory_collect,
    memory_collect_dual,
    memory_get_stats,
    memory_format_value,
    memory_format_dual_stats,
    NULL,
    memory_cleanup,
    "memory",
    "%",
    1,
    100.0
};
