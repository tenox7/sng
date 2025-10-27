#include "../datasource.h"
#include "../os/os_interface.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    double min_total;
    double max_total;
    double min_system;
    double max_system;
    uint64_t sum_total;
    uint64_t sum_system;
    uint32_t sample_count;
    double last_total;
    double last_system;
} cpu_context_t;

static int cpu_init(const char *target, void **context) {
    cpu_context_t *ctx = malloc(sizeof(cpu_context_t));
    if (!ctx) return 0;

    ctx->min_total = 100.0;
    ctx->max_total = 0.0;
    ctx->min_system = 100.0;
    ctx->max_system = 0.0;
    ctx->sum_total = 0;
    ctx->sum_system = 0;
    ctx->sample_count = 0;
    ctx->last_total = 0.0;
    ctx->last_system = 0.0;

    *context = ctx;
    return 1;
}

static int cpu_collect(void *context, double *value) {
    cpu_context_t *ctx;

    ctx = (cpu_context_t *)context;
    if (!ctx || !value) return 0;

    if (!os_cpu_get_stats(value)) {
        return 0;
    }

    if (*value < ctx->min_total) ctx->min_total = *value;
    if (*value > ctx->max_total) ctx->max_total = *value;
    ctx->sum_total += (uint64_t)*value;
    ctx->last_total = *value;
    ctx->sample_count++;

    return 1;
}

static int cpu_collect_dual(void *context, double *total_value, double *system_value) {
    cpu_context_t *ctx;

    ctx = (cpu_context_t *)context;
    if (!ctx || !total_value || !system_value) return 0;

    if (!os_cpu_get_stats_dual(total_value, system_value)) {
        return 0;
    }

    if (*total_value < ctx->min_total) ctx->min_total = *total_value;
    if (*total_value > ctx->max_total) ctx->max_total = *total_value;
    if (*system_value < ctx->min_system) ctx->min_system = *system_value;
    if (*system_value > ctx->max_system) ctx->max_system = *system_value;
    ctx->sum_total += (uint64_t)*total_value;
    ctx->sum_system += (uint64_t)*system_value;
    ctx->last_total = *total_value;
    ctx->last_system = *system_value;
    ctx->sample_count++;

    return 1;
}

static int cpu_get_stats(void *context, datasource_stats_t *stats) {
    cpu_context_t *ctx = (cpu_context_t *)context;
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

    stats->min = ctx->min_total;
    stats->max = ctx->max_total;
    stats->avg = (double)(ctx->sum_total / ctx->sample_count);
    stats->last = ctx->last_total;
    stats->min_secondary = ctx->min_system;
    stats->max_secondary = ctx->max_system;
    stats->avg_secondary = (double)(ctx->sum_system / ctx->sample_count);
    stats->last_secondary = ctx->last_system;

    return 1;
}

static void cpu_cleanup(void *context) {
    free(context);
}

static void cpu_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f%%", value);
}

datasource_handler_t cpu_handler = {
    .init = cpu_init,
    .collect = cpu_collect,
    .collect_dual = cpu_collect_dual,
    .get_stats = cpu_get_stats,
    .format_value = cpu_format_value,
    .cleanup = cpu_cleanup,
    .name = "cpu",
    .unit = "%",
    .is_dual = 1,
    .max_scale = 100.0
};
