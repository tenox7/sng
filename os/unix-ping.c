
#include "../compat.h"
#include "sryze-ping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#if defined(__hpux) || defined(UNIXWARE) || defined(__osf__) || defined(__digital__) || defined(sgi) || defined(__sgi) || defined(__sun) || defined(sun)
#include <netinet/in_systm.h>
#endif
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>

#if (defined(_AIX) && !defined(_AIX43)) || defined(__osf__) || defined(__digital__)
typedef int socklen_t;
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

struct icmp {
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint16_t icmp_cksum;
    uint16_t icmp_id;
    uint16_t icmp_seq;
};

#ifndef ICMP_ECHO
#define ICMP_ECHO 8
#endif
#ifndef ICMP_ECHOREPLY
#define ICMP_ECHOREPLY 0
#endif
#ifndef ICMP_MINLEN
#define ICMP_MINLEN 8
#endif

struct sunos_ping_context {
    int sockfd;
    int recvfd;
    struct sockaddr_in target_addr;
    uint16_t id;
    uint16_t seq;
    uint32_t timeout_us;
    char addr_str[INET_ADDRSTRLEN];
};

static uint16_t sunos_in_cksum(u_short *addr, int len)
{
    register int nleft = len;
    register u_short *w = addr;
    register u_short answer;
    u_short odd_byte;
    register int sum;

    odd_byte = 0;
    sum = 0;

    while (nleft > 1) {
        sum += *w++;
        nleft -= 2;
    }

    if (nleft == 1) {
        *(u_char *)(&odd_byte) = *(u_char *)w;
        sum += odd_byte;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return answer;
}

static uint64_t sunos_utime(void)
{
    struct timeval now;
    return gettimeofday(&now, NULL) != 0
        ? 0
        : now.tv_sec * 1000000 + now.tv_usec;
}

sryze_ping_context_t *sryze_ping_create(const char *hostname, uint32_t timeout_ms)
{
    struct sunos_ping_context *ctx;
    static uint16_t next_id = 0;
    int bufsize;
    struct hostent *hp;

    if (!hostname) return NULL;

    ctx = calloc(1, sizeof(struct sunos_ping_context));
    if (!ctx) return NULL;

    ctx->timeout_us = timeout_ms * 1000;
    ctx->id = (uint16_t)getpid() + (++next_id);
    ctx->seq = 0;
    strcpy(ctx->addr_str, "<unknown>");

    ctx->target_addr.sin_family = AF_INET;
    ctx->target_addr.sin_addr.s_addr = inet_addr(hostname);
    if (ctx->target_addr.sin_addr.s_addr == -1 && strcmp(hostname, "255.255.255.255") != 0) {
        hp = gethostbyname(hostname);
        if (hp && hp->h_length == 4) {
            memcpy(&ctx->target_addr.sin_addr, hp->h_addr, hp->h_length);
        } else {
            free(ctx);
            return NULL;
        }
    }

    ctx->recvfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (ctx->recvfd < 0) {
        free(ctx);
        return NULL;
    }

    ctx->sockfd = ctx->recvfd;

    bufsize = 48 * 1024;
    if (bufsize < 64)
        bufsize = 64;
    if (setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVBUF, (char *)&bufsize, sizeof(bufsize)) < 0) {
    }
    if (setsockopt(ctx->sockfd, SOL_SOCKET, SO_SNDBUF, (char *)&bufsize, sizeof(bufsize)) < 0) {
    }

#ifdef _AIX
    strncpy(ctx->addr_str, inet_ntoa(ctx->target_addr.sin_addr), sizeof(ctx->addr_str) - 1);
    ctx->addr_str[sizeof(ctx->addr_str) - 1] = '\0';
#else
    inet_ntop(AF_INET, &ctx->target_addr.sin_addr, ctx->addr_str, sizeof(ctx->addr_str));
#endif

    return (sryze_ping_context_t *)ctx;
}

int sryze_ping_send(sryze_ping_context_t *ctx_ptr, double *ping_time_ms)
{
    struct sunos_ping_context *ctx;
    struct icmp request;
    int error;
    uint64_t start_time;
    uint64_t delay;
    struct sockaddr_in from;
    socklen_t fromlen;
    int cc;
    struct ip *ip;
    int hlen;
    struct icmp *icp;

    if (!ctx_ptr || !ping_time_ms) return 0;

    ctx = (struct sunos_ping_context *)ctx_ptr;

    request.icmp_type = ICMP_ECHO;
    request.icmp_code = 0;
    request.icmp_cksum = 0;
    request.icmp_id = ctx->id;
    request.icmp_seq = ctx->seq++;

    request.icmp_cksum = sunos_in_cksum((u_short *)&request, sizeof(request));

    start_time = sunos_utime();

    error = sendto(ctx->sockfd, (char *)&request, sizeof(request), 0,
                   (struct sockaddr *)&ctx->target_addr, sizeof(ctx->target_addr));
    if (error < 0) {
        *ping_time_ms = -1.0;
        return 0;
    }

    for (;;) {
        char packet[1024];

        fromlen = sizeof(from);

        cc = recvfrom(ctx->recvfd, packet, sizeof(packet), 0,
                      (struct sockaddr *)&from, &fromlen);

        delay = sunos_utime() - start_time;

        if (cc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (delay > ctx->timeout_us) {
                    *ping_time_ms = -1.0;
                    return 0;
                } else {
                    continue;
                }
            } else {
                *ping_time_ms = -1.0;
                return 0;
            }
        }

        ip = (struct ip *)packet;
#if defined(__osf__) || defined(__digital__)
        hlen = (*(u_char *)packet & 0x0f) << 2;
#else
        hlen = ip->ip_hl << 2;
#endif
        if (cc < hlen + ICMP_MINLEN) {
            continue;
        }

        icp = (struct icmp *)(packet + hlen);
        if (ip->ip_p == 0) {
            icp = (struct icmp *)packet;
        }

        if (icp->icmp_type == ICMP_ECHOREPLY && icp->icmp_id == ctx->id) {
            *ping_time_ms = (double)delay / 1000.0;
            return 1;
        }
    }
}

void sryze_ping_destroy(sryze_ping_context_t *ctx_ptr)
{
    struct sunos_ping_context *ctx;

    if (!ctx_ptr) return;

    ctx = (struct sunos_ping_context *)ctx_ptr;
    if (ctx->sockfd >= 0) {
        close(ctx->sockfd);
    }
    free(ctx);
}
