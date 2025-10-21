#include "../datasource.h"
#include "../os/os_interface.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "../compat.h"

typedef struct {
    os_ping_context_t *ping_ctx;
    char *target;
    time_t last_dns_retry;
    int dns_failed;
    double min;
    double max;
    uint64_t sum;
    uint32_t sample_count;
    double last;
} ping_context_t;

static int ping_init(const char *target, void **context) {
    ping_context_t *ctx;

    if (!target) return 0;

    ctx = malloc(sizeof(ping_context_t));
    if (!ctx) return 0;

    ctx->target = malloc(strlen(target) + 1);
    if (!ctx->target) {
        free(ctx);
        return 0;
    }
    strcpy(ctx->target, target);
    ctx->ping_ctx = NULL;
    ctx->last_dns_retry = 0;
    ctx->dns_failed = 0;
    ctx->min = 10000.0;
    ctx->max = 0.0;
    ctx->sum = 0;
    ctx->sample_count = 0;
    ctx->last = 0.0;

    ctx->ping_ctx = os_ping_create(target, 1000);
    if (!ctx->ping_ctx) {
        ctx->dns_failed = 1;
        ctx->last_dns_retry = time(NULL);
    }

    *context = ctx;
    return 1;
}

static int ping_collect(void *context, double *value) {
    ping_context_t *ctx;
    time_t now;
    double ping_time;
    int success;

    ctx = (ping_context_t *)context;
    if (!ctx || !value) return 0;

    if (ctx->dns_failed || !ctx->ping_ctx) {
        now = time(NULL);
        if (now - ctx->last_dns_retry >= 30) {
            if (ctx->ping_ctx) {
                os_ping_destroy(ctx->ping_ctx);
                ctx->ping_ctx = NULL;
            }

            ctx->ping_ctx = os_ping_create(ctx->target, 1000);
            ctx->last_dns_retry = now;

            if (ctx->ping_ctx) {
                ctx->dns_failed = 0;
            } else {
                ctx->dns_failed = 1;
                *value = -1.0;
                return 0;
            }
        } else if (!ctx->ping_ctx) {
            *value = -1.0;
            return 0;
        }
    }

    success = os_ping_send(ctx->ping_ctx, &ping_time);

    *value = success ? ping_time : -1.0;

    if (success && *value >= 0.0) {
        if (*value < ctx->min) ctx->min = *value;
        if (*value > ctx->max) ctx->max = *value;
        ctx->sum += (uint64_t)*value;
        ctx->last = *value;
        ctx->sample_count++;
    }

    return success;
}

static int ping_get_stats(void *context, datasource_stats_t *stats) {
    ping_context_t *ctx = (ping_context_t *)context;
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

static void ping_cleanup(void *context) {
    ping_context_t *ctx = (ping_context_t *)context;
    if (!ctx) return;

    if (ctx->ping_ctx) {
        os_ping_destroy(ctx->ping_ctx);
    }
    free(ctx->target);
    free(ctx);
}

static void ping_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1fms", value);
}

datasource_handler_t ping_handler = {
    .init = ping_init,
    .collect = ping_collect,
    .collect_dual = NULL,
    .get_stats = ping_get_stats,
    .format_value = ping_format_value,
    .cleanup = ping_cleanup,
    .name = "ping",
    .unit = "ms",
    .is_dual = 0,
    .max_scale = 0.0
};
