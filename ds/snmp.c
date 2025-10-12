#include "../datasource.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../compat.h"
#include <time.h>

#ifdef __linux__
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifndef __linux__
#include <sys/types.h>
#include <sys/time.h>
#endif

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

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
    struct snmp_session *ses;

    // Statistics tracking in native uint32_t
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

static uint32_t getcntr32(struct snmp_session *ses, int dir, int inst) {
    struct snmp_pdu *pdu, *resp;
    oid iftable_oid[] = { 1,3,6,1,2,1,2,2,1,0,0 };
    int stat;
    uint32_t tmp;

    pdu = snmp_pdu_create(SNMP_MSG_GET);
    iftable_oid[9] = dir;
    iftable_oid[10] = inst;
    snmp_add_null_var(pdu, iftable_oid, sizeof(iftable_oid)/sizeof(oid));

    stat = snmp_synch_response(ses, pdu, &resp);
    if (stat != STAT_SUCCESS || resp->errstat != SNMP_ERR_NOERROR) {
        if (resp) snmp_free_pdu(resp);
        return 0;
    }

    if (resp->variables->type != ASN_COUNTER) {
        snmp_free_pdu(resp);
        return 0;
    }

    tmp = *(resp->variables->val.integer);

    snmp_free_pdu(resp);
    return tmp;
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
    struct snmp_session init_ses;

    if (!target) return 0;

    ctx = malloc(sizeof(snmp_context_t));
    if (!ctx) return 0;

    if (!parse_snmp_target(target, &ctx->hostname, &ctx->community, &ctx->interface_index)) {
        free(ctx);
        return 0;
    }

    init_snmp("sng");
    memset(&init_ses, 0, sizeof(struct snmp_session));
    snmp_sess_init(&init_ses);
    init_ses.version = SNMP_VERSION_1;
    init_ses.peername = ctx->hostname;
    init_ses.community = (unsigned char*)ctx->community;
    init_ses.community_len = strlen(ctx->community);

    ctx->ses = snmp_open(&init_ses);
    if (!ctx->ses) {
        free(ctx->hostname);
        free(ctx->community);
        free(ctx);
        return 0;
    }

    ctx->prev_in_octets = 0;
    ctx->prev_out_octets = 0;
    ctx->prev_time = 0;
    ctx->first_sample = 1;
    ctx->last_in_rate = 0.0;
    ctx->last_out_rate = 0.0;
    ctx->last_rate = 0.0;

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

    in_octets = getcntr32(ctx->ses, 10, ctx->interface_index);
    if (in_octets == 0) {
        return 0;
    }

    out_octets = getcntr32(ctx->ses, 16, ctx->interface_index);
    if (out_octets == 0) {
        return 0;
    }

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

    in_rate_bps = in_diff / time_diff;
    out_rate_bps = out_diff / time_diff;
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

    if (ctx->ses) {
        snmp_close(ctx->ses);
    }
    free(ctx->hostname);
    free(ctx->community);
    free(ctx);
}

static void snmp_format_value(double value, char *buffer, size_t buffer_size) {
    format_rate_human_readable(value, buffer, buffer_size);
}

datasource_handler_t snmp_handler = {
    .init = snmp_init,
    .collect = snmp_collect,
    .collect_dual = snmp_collect_dual,
    .get_stats = snmp_get_stats,
    .format_value = snmp_format_value,
    .cleanup = snmp_cleanup,
    .name = "snmp",
    .unit = "B/s",
    .is_dual = 1,
    .max_scale = 0.0
};
