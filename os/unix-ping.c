/*
 * Legacy Unix IPv4 ICMP echo ping.
 *
 * Uses SOCK_RAW + IPPROTO_ICMP (requires root) and gethostbyname for
 * portability to AIX, HP-UX, IRIX, Tru64, SunOS, and UnixWare. Modern
 * OSes (Darwin, Linux, FreeBSD) use icmp_ping.c instead.
 */

#include "os_interface.h"
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

struct icmp_echo_hdr {
    uint8_t  icmp_type;
    uint8_t  icmp_code;
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

struct os_ping_context_t {
    int sockfd;
    struct sockaddr_in target_addr;
    uint16_t id;
    uint16_t seq;
    uint32_t timeout_us;
};

static uint16_t unix_in_cksum(u_short *addr, int len)
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

static uint64_t unix_utime(void)
{
    struct timeval now;
    return gettimeofday(&now, NULL) != 0
        ? 0
        : now.tv_sec * 1000000 + now.tv_usec;
}

os_ping_context_t *os_ping_create(const char *hostname, uint32_t timeout_ms)
{
    os_ping_context_t *ctx;
    static uint16_t next_id = 0;
    int bufsize;
    struct hostent *hp;

    if (!hostname) return NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->timeout_us = timeout_ms * 1000;
    ctx->id = (uint16_t)getpid() + (++next_id);
    ctx->seq = 0;

    ctx->target_addr.sin_family = AF_INET;
    ctx->target_addr.sin_addr.s_addr = inet_addr(hostname);
    if (ctx->target_addr.sin_addr.s_addr == (in_addr_t)-1 && strcmp(hostname, "255.255.255.255") != 0) {
        hp = gethostbyname(hostname);
        if (hp && hp->h_length == 4) {
            memcpy(&ctx->target_addr.sin_addr, hp->h_addr, hp->h_length);
        } else {
            free(ctx);
            return NULL;
        }
    }

    ctx->sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (ctx->sockfd < 0) {
        free(ctx);
        return NULL;
    }

    bufsize = 48 * 1024;
    setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVBUF, (char *)&bufsize, sizeof(bufsize));
    setsockopt(ctx->sockfd, SOL_SOCKET, SO_SNDBUF, (char *)&bufsize, sizeof(bufsize));

    return ctx;
}

int os_ping_send(os_ping_context_t *ctx, double *ping_time_ms)
{
    struct icmp_echo_hdr request;
    int error;
    uint64_t start_time;
    uint64_t delay;
    struct sockaddr_in from;
    socklen_t fromlen;
    int cc;
    struct ip *ip;
    int hlen;
    struct icmp_echo_hdr *icp;

    if (!ctx || !ping_time_ms) return 0;

    request.icmp_type = ICMP_ECHO;
    request.icmp_code = 0;
    request.icmp_cksum = 0;
    request.icmp_id = ctx->id;
    request.icmp_seq = ctx->seq++;
    request.icmp_cksum = unix_in_cksum((u_short *)&request, sizeof(request));

    start_time = unix_utime();

    error = sendto(ctx->sockfd, (char *)&request, sizeof(request), 0,
                   (struct sockaddr *)&ctx->target_addr, sizeof(ctx->target_addr));
    if (error < 0) {
        *ping_time_ms = -1.0;
        return 0;
    }

    for (;;) {
        char packet[1024];

        fromlen = sizeof(from);

        cc = recvfrom(ctx->sockfd, packet, sizeof(packet), 0,
                      (struct sockaddr *)&from, &fromlen);

        delay = unix_utime() - start_time;

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

        icp = (struct icmp_echo_hdr *)(packet + hlen);
        if (ip->ip_p == 0) {
            icp = (struct icmp_echo_hdr *)packet;
        }

        if (icp->icmp_type == ICMP_ECHOREPLY && icp->icmp_id == ctx->id) {
            *ping_time_ms = (double)delay / 1000.0;
            return 1;
        }
    }
}

void os_ping_destroy(os_ping_context_t *ctx)
{
    if (!ctx) return;
    if (ctx->sockfd >= 0) {
        close(ctx->sockfd);
    }
    free(ctx);
}
