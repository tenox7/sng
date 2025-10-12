#include "../datasource.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include "../compat.h"
#include <sys/time.h>
#include <signal.h>

#ifdef _AIX
#include <sys/select.h>
#endif

typedef struct {
    char *command;
    FILE *fp;
    double min;
    double max;
    uint64_t sum;
    uint32_t sample_count;
    double last;
    int32_t refresh_interval_ms;
} shell_context_t;

static int parse_and_store_value(const char *token, const char *line, double *value) {
    char *endptr;
    double val;

    val = strtod(token, &endptr);
    if (endptr != token && val == val && val != (1.0/0.0) && val != (-1.0/0.0)) {
        if (strstr(line, "bytes from") && strstr(line, "time=")) {
            *value = val;
            return 1;
        }
        if (!strstr(line, "PING") && !strstr(line, "data bytes") && !strstr(line, "bytes from")) {
            *value = val;
            return 1;
        }
    }
    return 0;
}

static FILE* open_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    setvbuf(fp, NULL, _IONBF, 0);
    return fp;
}

static int shell_init(const char *target, void **context) {
    shell_context_t *ctx;

    if (!target) return 0;

    ctx = malloc(sizeof(shell_context_t));
    if (!ctx) return 0;

    ctx->command = strdup(target);
    if (!ctx->command) {
        free(ctx);
        return 0;
    }

    ctx->min = 1000000.0;
    ctx->max = 0.0;
    ctx->sum = 0;
    ctx->sample_count = 0;
    ctx->last = 0.0;
    ctx->refresh_interval_ms = 0;

    ctx->fp = open_command(ctx->command);
    if (!ctx->fp) {
        free(ctx->command);
        free(ctx);
        return 0;
    }

    *context = ctx;
    return 1;
}

static int shell_collect(void *context, double *value) {
    shell_context_t *ctx;
    int fd;
    fd_set readfds;
    struct timeval timeout;
    char line[1024];
    char last_line[1024];
    int got_line;
    int drain_count;
    int ret;
    int got_value;
    char *token;

    ctx = (shell_context_t *)context;
    if (!ctx || !value) return 0;

    if (!ctx->fp) {
        ctx->fp = open_command(ctx->command);
        if (!ctx->fp) return 0;
    }

    if (feof(ctx->fp)) {
        pclose(ctx->fp);
        ctx->fp = open_command(ctx->command);
        if (!ctx->fp) return 0;
    }

    fd = fileno(ctx->fp);
    got_line = 0;
    drain_count = 0;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret <= 0) {
            if (got_line) {
                break;
            }
            return 0;
        }

        if (!fgets(line, sizeof(line), ctx->fp)) {
            if (got_line) {
                clearerr(ctx->fp);
                break;
            }

            if (feof(ctx->fp)) {
                pclose(ctx->fp);
                ctx->fp = open_command(ctx->command);
                if (!ctx->fp) return 0;
                fd = fileno(ctx->fp);
                clearerr(ctx->fp);

                usleep(10000);

                FD_ZERO(&readfds);
                FD_SET(fd, &readfds);
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000;
                ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
                if (ret <= 0) {
                    return 0;
                }
                continue;
            }

            clearerr(ctx->fp);
            return 0;
        }

        strcpy(last_line, line);
        got_line = 1;
        drain_count++;
    }

    if (drain_count > 0) {
    }

    if (!got_line) {
        return 0;
    }

    got_value = 0;
    token = strtok(last_line, " \t\r\n");
    while (token != NULL) {
        if (parse_and_store_value(token, last_line, value)) {
            got_value = 1;
            break;
        }
        token = strtok(NULL, " \t\r\n");
    }

    if (got_value) {
        if (*value < ctx->min) ctx->min = *value;
        if (*value > ctx->max) ctx->max = *value;
        ctx->sum += (uint64_t)*value;
        ctx->last = *value;
        ctx->sample_count++;
        clearerr(ctx->fp);
    } else {
    }

    return got_value;
}

static int shell_get_stats(void *context, datasource_stats_t *stats) {
    shell_context_t *ctx = (shell_context_t *)context;
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

static void shell_cleanup(void *context) {
    shell_context_t *ctx = (shell_context_t *)context;
    if (!ctx) return;

    if (ctx->fp) {
        pclose(ctx->fp);
    }

    free(ctx->command);
    free(ctx);
}

static void shell_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f", value);
}

void shell_set_refresh_interval(void *context, int32_t refresh_interval_ms) {
    shell_context_t *ctx = (shell_context_t *)context;
    if (!ctx) return;
    ctx->refresh_interval_ms = refresh_interval_ms;
}

datasource_handler_t shell_handler = {
    .init = shell_init,
    .collect = shell_collect,
    .collect_dual = NULL,
    .get_stats = shell_get_stats,
    .format_value = shell_format_value,
    .cleanup = shell_cleanup,
    .name = "shell",
    .unit = "",
    .is_dual = 0,
    .max_scale = 0.0
};
