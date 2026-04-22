/*
 * IPv4 ICMP echo ping.
 *
 * Opens SOCK_DGRAM + IPPROTO_ICMP first (unprivileged: works on macOS out of
 * the box, and on Linux when net.ipv4.ping_group_range covers the caller's
 * gid). Falls back to SOCK_RAW + IPPROTO_ICMP if SOCK_DGRAM is not permitted.
 *
 * Reply matching uses the source address and sequence number. Under SOCK_DGRAM
 * the kernel rewrites the ICMP id on send and demuxes replies to the originating
 * socket, so matching on id is unreliable across platforms.
 */

#include "os_interface.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

struct icmp_echo_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t cksum;
    uint16_t id;
    uint16_t seq;
};

struct os_ping_context_t {
    int sockfd;
    struct sockaddr_in dst;
    uint16_t id;
    uint16_t seq;
    uint32_t timeout_ms;
};

static uint16_t icmp_cksum(const void *data, size_t len) {
    const uint16_t *w = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *w++; len -= 2; }
    if (len) sum += *(const uint8_t *)w;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint64_t now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

os_ping_context_t *os_ping_create(const char *hostname, uint32_t timeout_ms) {
    struct addrinfo hints, *ai = NULL;
    os_ping_context_t *ctx;
    int fd;

    if (!hostname) return NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(hostname, NULL, &hints, &ai) != 0 || !ai) return NULL;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) { freeaddrinfo(ai); return NULL; }

    ctx = (os_ping_context_t *)calloc(1, sizeof(*ctx));
    if (!ctx) { close(fd); freeaddrinfo(ai); return NULL; }

    memcpy(&ctx->dst, ai->ai_addr, sizeof(ctx->dst));
    ctx->dst.sin_port = 0;
    ctx->sockfd = fd;
    ctx->id = (uint16_t)(getpid() & 0xffff);
    ctx->seq = 0;
    ctx->timeout_ms = timeout_ms ? timeout_ms : 1000;

    freeaddrinfo(ai);
    return ctx;
}

int os_ping_send(os_ping_context_t *ctx, double *ping_time_ms) {
    struct icmp_echo_hdr req;
    uint8_t buf[1500];
    struct sockaddr_in from;
    socklen_t fromlen;
    fd_set rfds;
    struct timeval tv;
    uint64_t t0, elapsed_us, timeout_us;
    uint16_t seq;
    ssize_t n;
    int r;

    if (!ctx || !ping_time_ms) return 0;

    seq = ++ctx->seq;
    memset(&req, 0, sizeof(req));
    req.type = ICMP_ECHO_REQUEST;
    req.id = htons(ctx->id);
    req.seq = htons(seq);
    req.cksum = icmp_cksum(&req, sizeof(req));

    timeout_us = (uint64_t)ctx->timeout_ms * 1000ULL;
    t0 = now_us();

    if (sendto(ctx->sockfd, &req, sizeof(req), 0,
               (struct sockaddr *)&ctx->dst, sizeof(ctx->dst)) < 0) {
        *ping_time_ms = -1.0;
        return 0;
    }

    for (;;) {
        const struct icmp_echo_hdr *reply;
        size_t off;

        elapsed_us = now_us() - t0;
        if (elapsed_us >= timeout_us) {
            *ping_time_ms = -1.0;
            return 0;
        }
        tv.tv_sec = (time_t)((timeout_us - elapsed_us) / 1000000ULL);
        tv.tv_usec = (suseconds_t)((timeout_us - elapsed_us) % 1000000ULL);

        FD_ZERO(&rfds);
        FD_SET(ctx->sockfd, &rfds);
        r = select(ctx->sockfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { *ping_time_ms = -1.0; return 0; }

        fromlen = sizeof(from);
        n = recvfrom(ctx->sockfd, buf, sizeof(buf), 0,
                     (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            *ping_time_ms = -1.0;
            return 0;
        }

        if (from.sin_addr.s_addr != ctx->dst.sin_addr.s_addr) continue;

        /* SOCK_RAW delivers IP+ICMP; SOCK_DGRAM usually delivers ICMP only.
         * Detect an IPv4 header by its version nibble and skip it. */
        off = 0;
        if (n > 0 && (buf[0] >> 4) == 4) {
            off = (size_t)(buf[0] & 0x0f) * 4;
        }
        if ((size_t)n < off + sizeof(struct icmp_echo_hdr)) continue;
        reply = (const struct icmp_echo_hdr *)(buf + off);

        if (reply->type != ICMP_ECHO_REPLY) continue;
        if (ntohs(reply->seq) != seq) continue;

        *ping_time_ms = (double)(now_us() - t0) / 1000.0;
        return 1;
    }
}

void os_ping_destroy(os_ping_context_t *ctx) {
    if (!ctx) return;
    if (ctx->sockfd >= 0) close(ctx->sockfd);
    free(ctx);
}
