#include "../datasource.h"
#include "snmp_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../compat.h"
#include <time.h>

typedef struct {
    char *hostname;
    char *community;
    int interface_index;
    uint32_t prev_in_octets;
    uint32_t prev_out_octets;
    time_t prev_time;
    int first_sample;
    uint32_t last_in_rate;
    uint32_t last_out_rate;
    uint32_t last_rate;
    uint32_t min_in_rate;
    uint32_t max_in_rate;
    uint32_t min_out_rate;
    uint32_t max_out_rate;
    uint32_t min_combined_rate;
    uint32_t max_combined_rate;
    uint64_t sum_in_rate;
    uint64_t sum_out_rate;
    uint64_t sum_combined_rate;
    uint32_t sample_count;
} snmp_context_t;

static int getcntr32(const char *host, const char *community, int dir, int inst, uint32_t *result) {
    uint32_t iftable_oid[] = { 1,3,6,1,2,1,2,2,1,0,0 };

    iftable_oid[9] = dir;
    iftable_oid[10] = inst;

    return snmp_get_counter32(host, community, iftable_oid, 11, result);
}

static void format_rate_human_readable(double disp_bytes_per_sec, char* buffer, size_t buffer_size) {
    if (disp_bytes_per_sec >= 1073741824.0) {
        snprintf(buffer, buffer_size, "%.1f GB/s", disp_bytes_per_sec / 1073741824.0);
    } else if (disp_bytes_per_sec >= 1048576.0) {
        snprintf(buffer, buffer_size, "%.1f MB/s", disp_bytes_per_sec / 1048576.0);
    } else if (disp_bytes_per_sec >= 1024.0) {
        snprintf(buffer, buffer_size, "%.1f KB/s", disp_bytes_per_sec / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%.1f B/s", disp_bytes_per_sec);
    }
}

static int parse_snmp_target(const char* target, char** hostname, char** community, int* interface_index) {
    char* target_copy;
    char* hostname_str;
    char* community_str;
    char* interface_str;

    if (!target) return 0;

    target_copy = strdup(target);
    hostname_str = strtok(target_copy, ",");
    community_str = strtok(NULL, ",");
    interface_str = strtok(NULL, ",");

    if (!hostname_str || !community_str || !interface_str) {
        free(target_copy);
        return 0;
    }

    *hostname = strdup(hostname_str);
    *community = strdup(community_str);
    *interface_index = atoi(interface_str);

    free(target_copy);
    return (*hostname && *community);
}

static int snmp_init(const char *target, void **context) {
    snmp_context_t *ctx;

    if (!target) return 0;

    ctx = malloc(sizeof(snmp_context_t));
    if (!ctx) return 0;

    if (!parse_snmp_target(target, &ctx->hostname, &ctx->community, &ctx->interface_index)) {
        free(ctx);
        return 0;
    }

    ctx->prev_in_octets = 0;
    ctx->prev_out_octets = 0;
    ctx->prev_time = 0;
    ctx->first_sample = 1;
    ctx->last_in_rate = 0;
    ctx->last_out_rate = 0;
    ctx->last_rate = 0;
    ctx->min_in_rate = UINT32_MAX;
    ctx->max_in_rate = 0;
    ctx->min_out_rate = UINT32_MAX;
    ctx->max_out_rate = 0;
    ctx->min_combined_rate = UINT32_MAX;
    ctx->max_combined_rate = 0;
    ctx->sum_in_rate = 0;
    ctx->sum_out_rate = 0;
    ctx->sum_combined_rate = 0;
    ctx->sample_count = 0;

    *context = ctx;
    return 1;
}

static int snmp_collect_internal(snmp_context_t *ctx) {
    uint32_t in_octets, out_octets;
    time_t current_time;
    time_t time_diff;
    uint32_t in_diff, out_diff;
    uint32_t in_rate_bps, out_rate_bps, combined_rate_bps;

    current_time = time(NULL);

    if (!getcntr32(ctx->hostname, ctx->community, 10, ctx->interface_index, &in_octets))
        return 0;

    if (!getcntr32(ctx->hostname, ctx->community, 16, ctx->interface_index, &out_octets))
        return 0;

    if (ctx->first_sample) {
        ctx->prev_in_octets = in_octets;
        ctx->prev_out_octets = out_octets;
        ctx->prev_time = current_time;
        ctx->first_sample = 0;
        ctx->last_in_rate = 0;
        ctx->last_out_rate = 0;
        ctx->last_rate = 0;
        return 1;
    }

    time_diff = current_time - ctx->prev_time;
    if (time_diff <= 0) {
        return 1;
    }

    in_diff = in_octets - ctx->prev_in_octets;
    out_diff = out_octets - ctx->prev_out_octets;

    in_rate_bps = in_diff / (uint32_t)time_diff;
    out_rate_bps = out_diff / (uint32_t)time_diff;
    combined_rate_bps = in_rate_bps + out_rate_bps;

    if (in_rate_bps < ctx->min_in_rate) ctx->min_in_rate = in_rate_bps;
    if (in_rate_bps > ctx->max_in_rate) ctx->max_in_rate = in_rate_bps;
    if (out_rate_bps < ctx->min_out_rate) ctx->min_out_rate = out_rate_bps;
    if (out_rate_bps > ctx->max_out_rate) ctx->max_out_rate = out_rate_bps;
    if (combined_rate_bps < ctx->min_combined_rate) ctx->min_combined_rate = combined_rate_bps;
    if (combined_rate_bps > ctx->max_combined_rate) ctx->max_combined_rate = combined_rate_bps;

    ctx->sum_in_rate += in_rate_bps;
    ctx->sum_out_rate += out_rate_bps;
    ctx->sum_combined_rate += combined_rate_bps;
    ctx->sample_count++;

    ctx->prev_in_octets = in_octets;
    ctx->prev_out_octets = out_octets;
    ctx->prev_time = current_time;
    ctx->last_in_rate = in_rate_bps;
    ctx->last_out_rate = out_rate_bps;
    ctx->last_rate = combined_rate_bps;

    return 1;
}

static int snmp_collect(void *context, double *disp_value) {
    snmp_context_t *ctx = (snmp_context_t *)context;
    if (!ctx || !disp_value) return 0;

    if (!snmp_collect_internal(ctx)) {
        *disp_value = -1.0;
        return 0;
    }

    *disp_value = (double)ctx->last_rate;
    return 1;
}

static int snmp_collect_dual(void *context, double *disp_in_value, double *disp_out_value) {
    snmp_context_t *ctx = (snmp_context_t *)context;
    if (!ctx || !disp_in_value || !disp_out_value) return 0;

    if (!snmp_collect_internal(ctx)) {
        *disp_in_value = -1.0;
        *disp_out_value = -1.0;
        return 0;
    }

    *disp_in_value = (double)ctx->last_in_rate;
    *disp_out_value = (double)ctx->last_out_rate;
    return 1;
}

static int snmp_get_stats(void *context, datasource_stats_t *stats) {
    snmp_context_t *ctx = (snmp_context_t *)context;
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

    // Convert from uint32_t to double for display
    stats->min = (double)ctx->min_in_rate;
    stats->max = (double)ctx->max_in_rate;
    stats->avg = (double)(ctx->sum_in_rate / ctx->sample_count);
    stats->last = (double)ctx->last_in_rate;
    stats->min_secondary = (double)ctx->min_out_rate;
    stats->max_secondary = (double)ctx->max_out_rate;
    stats->avg_secondary = (double)(ctx->sum_out_rate / ctx->sample_count);
    stats->last_secondary = (double)ctx->last_out_rate;

    return 1;
}

static void snmp_cleanup(void *context) {
    snmp_context_t *ctx = (snmp_context_t *)context;
    if (!ctx) return;

    free(ctx->hostname);
    free(ctx->community);
    free(ctx);
}

static void snmp_format_value(double value, char *buffer, size_t buffer_size) {
    format_rate_human_readable(value, buffer, buffer_size);
}

static void snmp_format_dual_stats(double in_value, double out_value, char *buffer, size_t buffer_size) {
    char in_str[32];
    char out_str[32];
    format_rate_human_readable(in_value, in_str, sizeof(in_str));
    format_rate_human_readable(out_value, out_str, sizeof(out_str));
    snprintf(buffer, buffer_size, "%s/%s", in_str, out_str);
}

datasource_handler_t snmp_handler = {
    .init = snmp_init,
    .collect = snmp_collect,
    .collect_dual = snmp_collect_dual,
    .get_stats = snmp_get_stats,
    .format_value = snmp_format_value,
    .format_dual_stats = snmp_format_dual_stats,
    .cleanup = snmp_cleanup,
    .name = "snmp",
    .unit = "B/s",
    .is_dual = 1,
    .max_scale = 0.0
};
