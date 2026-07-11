#ifdef __VMS
#include "datasource.h"
#else
#include "../datasource.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
typedef SOCKET sock_t;
#define close closesocket
#define SOCKERR() WSAGetLastError()
#define ERR_INPROGRESS WSAEWOULDBLOCK
#define ERR_INTR WSAEINTR
#else
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#if defined(_AIX)
#include <sys/select.h>
#endif
#if defined(__VMS)
typedef unsigned int socklen_t;
#elif (defined(_AIX) && !defined(_AIX43)) || defined(__osf__) || defined(__digital__)
typedef int socklen_t;
#endif
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define SOCKERR() errno
#define ERR_INPROGRESS EINPROGRESS
#define ERR_INTR EINTR
#endif

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

#define TCP_TIMEOUT_MS 3000
#define TCP_RESOLVE_RETRY_SEC 30

typedef struct {
    char *host;
    struct sockaddr_in dst;
    int resolved;
    time_t last_retry;
    double min;
    double max;
    double sum;
    double last;
    uint32_t sample_count;
} tcp_context_t;

static uint64_t tcp_now_us(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart * 1000000 / f.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
#endif
}

static int tcp_resolve(tcp_context_t *ctx) {
    struct hostent *he;
    unsigned long addr;

    addr = inet_addr(ctx->host);
    if (addr != INADDR_NONE) {
        ctx->dst.sin_addr.s_addr = (uint32_t)addr;
        return 1;
    }

    he = gethostbyname(ctx->host);
    if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) return 0;
    memcpy(&ctx->dst.sin_addr, he->h_addr_list[0], sizeof(ctx->dst.sin_addr));
    return 1;
}

static int tcp_init(const char *target, void **context) {
    tcp_context_t *ctx;
    const char *sep;
    size_t hostlen;
    int port;
#ifdef _WIN32
    WSADATA wsa;
#endif

    if (!target) return 0;

    sep = strchr(target, ':');
    if (!sep) sep = strchr(target, ',');
    if (!sep || sep == target) return 0;

    port = atoi(sep + 1);
    if (port < 1 || port > 65535) return 0;

    ctx = calloc(1, sizeof(tcp_context_t));
    if (!ctx) return 0;

    hostlen = sep - target;
    ctx->host = malloc(hostlen + 1);
    if (!ctx->host) {
        free(ctx);
        return 0;
    }
    memcpy(ctx->host, target, hostlen);
    ctx->host[hostlen] = '\0';

    ctx->dst.sin_family = AF_INET;
    ctx->dst.sin_port = htons((uint16_t)port);
    ctx->min = 10000.0;

#ifdef _WIN32
    WSAStartup(MAKEWORD(1, 1), &wsa);
#endif

    ctx->resolved = tcp_resolve(ctx);
    if (!ctx->resolved) ctx->last_retry = time(NULL);

    *context = ctx;
    return 1;
}

static int tcp_collect(void *context, double *value) {
    tcp_context_t *ctx;
    sock_t fd;
    fd_set wfds, efds;
    struct timeval tv;
    uint64_t t0, elapsed_us, timeout_us;
    time_t now;
    int r, err;
    socklen_t elen;
#ifdef _WIN32
    u_long nonblock;
#endif

    ctx = (tcp_context_t *)context;
    if (!ctx || !value) return 0;
    *value = -1.0;

    if (!ctx->resolved) {
        now = time(NULL);
        if (now - ctx->last_retry < TCP_RESOLVE_RETRY_SEC) return 0;
        ctx->last_retry = now;
        ctx->resolved = tcp_resolve(ctx);
        if (!ctx->resolved) return 0;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return 0;

#ifdef _WIN32
    nonblock = 1;
    ioctlsocket(fd, FIONBIO, &nonblock);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif

    timeout_us = (uint64_t)TCP_TIMEOUT_MS * 1000;
    t0 = tcp_now_us();

    r = connect(fd, (struct sockaddr *)&ctx->dst, sizeof(ctx->dst));
    if (r < 0 && SOCKERR() != ERR_INPROGRESS) {
        close(fd);
        return 0;
    }

    if (r < 0) {
        for (;;) {
            elapsed_us = tcp_now_us() - t0;
            if (elapsed_us >= timeout_us) {
                close(fd);
                return 0;
            }
            tv.tv_sec = (long)((timeout_us - elapsed_us) / 1000000);
            tv.tv_usec = (long)((timeout_us - elapsed_us) % 1000000);

            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            FD_ZERO(&efds);
            FD_SET(fd, &efds);

            /* winsock signals failed connect on the except set only */
            r = select((int)fd + 1, NULL, &wfds, &efds, &tv);
            if (r < 0 && SOCKERR() == ERR_INTR) continue;
            if (r <= 0 || FD_ISSET(fd, &efds)) {
                close(fd);
                return 0;
            }
            break;
        }

        err = 0;
        elen = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) < 0 || err != 0) {
            close(fd);
            return 0;
        }
    }

    *value = (double)(tcp_now_us() - t0) / 1000.0;
    close(fd);

    if (*value < ctx->min) ctx->min = *value;
    if (*value > ctx->max) ctx->max = *value;
    ctx->sum += *value;
    ctx->last = *value;
    ctx->sample_count++;

    return 1;
}

static int tcp_get_stats(void *context, datasource_stats_t *stats) {
    tcp_context_t *ctx = (tcp_context_t *)context;
    if (!ctx || !stats) return 0;

    stats->min_secondary = 0.0;
    stats->max_secondary = 0.0;
    stats->avg_secondary = 0.0;
    stats->last_secondary = 0.0;

    if (ctx->sample_count == 0) {
        stats->min = 0.0;
        stats->max = 0.0;
        stats->avg = 0.0;
        stats->last = 0.0;
        return 1;
    }

    stats->min = ctx->min;
    stats->max = ctx->max;
    stats->avg = ctx->sum / ctx->sample_count;
    stats->last = ctx->last;
    return 1;
}

static void tcp_cleanup(void *context) {
    tcp_context_t *ctx = (tcp_context_t *)context;
    if (!ctx) return;
    free(ctx->host);
    free(ctx);
}

static void tcp_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1fms", value);
}

datasource_handler_t tcp_handler = {
    tcp_init,
    tcp_collect,
    NULL,
    tcp_get_stats,
    tcp_format_value,
    NULL,
    NULL,
    tcp_cleanup,
    "tcp",
    "ms",
    0,
    0.0
};
