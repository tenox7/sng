/* https://github.com/sryze/ping */

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
#endif

#include "../compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#undef CMSG_SPACE
#define CMSG_SPACE WSA_CMSG_SPACE
#undef CMSG_FIRSTHDR
#define CMSG_FIRSTHDR WSA_CMSG_FIRSTHDR
#undef CMSG_NXTHDR
#define CMSG_NXTHDR WSA_CMSG_NXTHDR
#undef CMSG_DATA
#define CMSG_DATA WSA_CMSG_DATA

typedef SOCKET socket_t;
typedef WSAMSG msghdr_t;
typedef WSACMSGHDR cmsghdr_t;

static LPFN_WSARECVMSG WSARecvMsg;

#else

#ifdef __APPLE__
    #define __APPLE_USE_RFC_3542
#endif

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

typedef int socket_t;
typedef struct msghdr msghdr_t;
typedef struct cmsghdr cmsghdr_t;

#endif

#define IP_VERSION_ANY 0
#define IP_V4 4
#define IP_V6 6

#define ICMP_HEADER_LENGTH 8
#define MESSAGE_BUFFER_SIZE 1024

#ifndef ICMP_ECHO
    #define ICMP_ECHO 8
#endif
#ifndef ICMP_ECHO6
    #define ICMP6_ECHO 128
#endif
#ifndef ICMP_ECHO_REPLY
    #define ICMP_ECHO_REPLY 0
#endif
#ifndef ICMP_ECHO_REPLY6
    #define ICMP6_ECHO_REPLY 129
#endif

#define REQUEST_TIMEOUT 1000000
#define REQUEST_INTERVAL 1000000

#ifdef _WIN32
    #define socket(af, type, protocol) \
        WSASocketW(af, type, protocol, NULL, 0, 0)
    #define close_socket closesocket
    #define getpid _getpid
    #define usleep(usec) Sleep((DWORD)((usec) / 1000))
#else
    #define close_socket close
#endif

#pragma pack(push, 1)

#if defined _WIN32 || defined __CYGWIN__

#if defined _MSC_VER || defined __MINGW32__
    typedef unsigned __int8 uint8_t;
    typedef unsigned __int16 uint16_t;
    typedef unsigned __int32 uint32_t;
    typedef unsigned __int64 uint64_t;
#endif

struct icmp {
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint16_t icmp_cksum;
    uint16_t icmp_id;
    uint16_t icmp_seq;
};

#endif

struct ip6_pseudo_hdr {
    struct in6_addr src;
    struct in6_addr dst;
    uint8_t unused1[2];
    uint16_t plen;
    uint8_t unused2[3];
    uint8_t nxt;
};

struct icmp6_packet {
    struct ip6_pseudo_hdr ip6_hdr;
    struct icmp icmp;
};

#pragma pack(pop)

#ifdef _WIN32

static void psockerror(const char *s)
{
    char *message;
    DWORD format_flags;
    DWORD result;

    message = NULL;
    format_flags = FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS
        | FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_MAX_WIDTH_MASK;

    result = FormatMessageA(format_flags,
                            NULL,
                            WSAGetLastError(),
                            0,
                            (char *)&message,
                            0,
                            NULL);
    if (result > 0) {
        LocalFree(message);
    } else {
        fprintf(stderr, "%s: Unknown error\n", s);
    }
}

#else

#define psockerror perror

#endif

static uint64_t get_utime(void)
{
#ifdef _WIN32
    LARGE_INTEGER count;
    LARGE_INTEGER frequency;
    if (QueryPerformanceCounter(&count) == 0
        || QueryPerformanceFrequency(&frequency) == 0) {
        return 0;
    }
    return count.QuadPart * 1000000 / frequency.QuadPart;
#else
    struct timeval now;
    return gettimeofday(&now, NULL) != 0
        ? 0
        : now.tv_sec * 1000000 + now.tv_usec;
#endif
}

#ifdef _WIN32

static void init_winsock_lib(void)
{
    int error;
    WSADATA wsa_data;

    error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (error != 0) {
        exit(EXIT_FAILURE);
    }
}

static void init_winsock_extensions(socket_t sockfd)
{
    int error;
    GUID recvmsg_id;
    DWORD size;

    recvmsg_id = WSAID_WSARECVMSG;

    error = WSAIoctl(sockfd,
                     SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &recvmsg_id,
                     sizeof(recvmsg_id),
                     &WSARecvMsg,
                     sizeof(WSARecvMsg),
                     &size,
                     NULL,
                     NULL);
    if (error == SOCKET_ERROR) {
        psockerror("WSAIoctl");
        exit(EXIT_FAILURE);
    }
}

#endif

static uint16_t compute_checksum(const char *buf, size_t size)
{

    size_t i;
    uint64_t sum;

    sum = 0;

    for (i = 0; i < size; i += 2) {
        sum += *(uint16_t *)buf;
        buf += 2;
    }
    if (size - i > 0)
        sum += *(uint8_t *)buf;

    while ((sum >> 16) != 0)
        sum = (sum & 0xffff) + (sum >> 16);

    return (uint16_t)~sum;
}

#include "sryze-ping.h"

struct sryze_ping_context {
    socket_t sockfd;
    struct sockaddr_storage addr;
    socklen_t dst_addr_len;
    uint16_t id;
    uint16_t seq;
    uint32_t timeout_us;
    char addr_str[INET6_ADDRSTRLEN];
};

sryze_ping_context_t *sryze_ping_create(const char *hostname, uint32_t timeout_ms)
{
    if (!hostname) return NULL;

    sryze_ping_context_t *ctx = calloc(1, sizeof(sryze_ping_context_t));
    if (!ctx) return NULL;

    int ip_version;
    int error;
    struct addrinfo *addrinfo_list;
    struct addrinfo *addrinfo;

    ip_version = IP_VERSION_ANY;
    addrinfo_list = NULL;

    ctx->timeout_us = timeout_ms * 1000;
    static uint16_t next_id = 0;
    ctx->id = (uint16_t)getpid() + (++next_id);
    ctx->seq = 0;
    strcpy(ctx->addr_str, "<unknown>");

#ifdef _WIN32
    init_winsock_lib();
#endif

    if (ip_version == IP_V4 || ip_version == IP_VERSION_ANY) {
        struct addrinfo hints;
        hints = (struct addrinfo){0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = IPPROTO_ICMP;
        error = getaddrinfo(hostname,
                            NULL,
                            &hints,
                            &addrinfo_list);
    }
    if (ip_version == IP_V6
        || (ip_version == IP_VERSION_ANY && error != 0)) {
        struct addrinfo hints;
        hints = (struct addrinfo){0};
        hints.ai_family = AF_INET6;
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = IPPROTO_ICMPV6;
        error = getaddrinfo(hostname,
                            NULL,
                            &hints,
                            &addrinfo_list);
    }
    if (error != 0) {
        goto cleanup_error;
    }

    for (addrinfo = addrinfo_list;
        addrinfo != NULL;
        addrinfo = addrinfo->ai_next) {
        ctx->sockfd = socket(addrinfo->ai_family,
                        addrinfo->ai_socktype,
                        addrinfo->ai_protocol);
        if (ctx->sockfd >= 0) {
            break;
        }
    }

    if ((int)ctx->sockfd < 0) {
        goto cleanup_error;
    }

    memcpy(&ctx->addr, addrinfo->ai_addr, addrinfo->ai_addrlen);
    ctx->dst_addr_len = (socklen_t)addrinfo->ai_addrlen;

    freeaddrinfo(addrinfo_list);
    addrinfo = NULL;
    addrinfo_list = NULL;

#ifdef _WIN32
    init_winsock_extensions(ctx->sockfd);
#endif

#ifdef _WIN32
    {
        u_long opt_value;
        opt_value = 1;
        if (ioctlsocket(ctx->sockfd, FIONBIO, &opt_value) != 0) {
            goto cleanup_error;
        }
    }
#else
    if (fcntl(ctx->sockfd, F_SETFL, O_NONBLOCK) == -1) {
        goto cleanup_error;
    }
#endif

    if (ctx->addr.ss_family == AF_INET6) {
#ifndef __sun__
        int opt_value;
        opt_value = 1;
        error = setsockopt(ctx->sockfd,
                           IPPROTO_IPV6,
#if defined _WIN32 || defined __CYGWIN__
                           IPV6_PKTINFO,
#else
                           IPV6_RECVPKTINFO,
#endif
                           (char *)&opt_value,
                           sizeof(opt_value));
        if (error != 0) {
            goto cleanup_error;
        }
#endif
    }

#if !defined _WIN32
    if (setgid(getgid()) != 0) {
        goto cleanup_error;
    }
    if (setuid(getuid()) != 0) {
        goto cleanup_error;
    }
#endif

    inet_ntop(ctx->addr.ss_family,
              ctx->addr.ss_family == AF_INET6
                  ? (void *)&((struct sockaddr_in6 *)&ctx->addr)->sin6_addr
                  : (void *)&((struct sockaddr_in *)&ctx->addr)->sin_addr,
              ctx->addr_str,
              sizeof(ctx->addr_str));

    return ctx;

cleanup_error:
    if (addrinfo_list != NULL) {
        freeaddrinfo(addrinfo_list);
    }
    if (ctx->sockfd >= 0) {
        close_socket(ctx->sockfd);
    }
    free(ctx);
    return NULL;
}

int sryze_ping_send(sryze_ping_context_t *ctx, double *ping_time_ms)
{
    if (!ctx || !ping_time_ms) return 0;

    struct icmp request;
    int error;
    uint64_t start_time;
    uint64_t delay;

    request.icmp_type =
            ctx->addr.ss_family == AF_INET6 ? ICMP6_ECHO : ICMP_ECHO;
    request.icmp_code = 0;
    request.icmp_cksum = 0;
    request.icmp_id = htons(ctx->id);
    request.icmp_seq = htons(ctx->seq++);

    if (ctx->addr.ss_family == AF_INET6) {
        struct icmp6_packet request_packet;
        request_packet = (struct icmp6_packet){0};

        request_packet.ip6_hdr.src = in6addr_loopback;
        request_packet.ip6_hdr.dst =
            ((struct sockaddr_in6 *)&ctx->addr)->sin6_addr;
        request_packet.ip6_hdr.plen = htons((uint16_t)ICMP_HEADER_LENGTH);
        request_packet.ip6_hdr.nxt = IPPROTO_ICMPV6;
        request_packet.icmp = request;

        request.icmp_cksum = compute_checksum((char *)&request_packet,
                                              sizeof(request_packet));
    } else {
        request.icmp_cksum = compute_checksum((char *)&request,
                                              sizeof(request));
    }

    start_time = get_utime();

    error = (int)sendto(ctx->sockfd,
                        (char *)&request,
                        sizeof(request),
                        0,
                        (struct sockaddr *)&ctx->addr,
                        (int)ctx->dst_addr_len);
    if (error < 0) {
        *ping_time_ms = -1.0;
        return 0;
    }

    for (;;) {
        char msg_buf[MESSAGE_BUFFER_SIZE];
        char packet_info_buf[MESSAGE_BUFFER_SIZE];
        struct in6_addr msg_addr;
        msg_addr = (struct in6_addr){0};
#ifdef _WIN32
        WSABUF msg_buf_struct;
        WSAMSG msg;
        DWORD msg_len;

        msg_buf_struct = (WSABUF){
            sizeof(msg_buf),
            msg_buf
        };
        msg = (WSAMSG){
            NULL,
            0,
            &msg_buf_struct,
            1,
            {sizeof(packet_info_buf), packet_info_buf},
            0
        };
        msg_len = 0;
#else
        struct iovec msg_buf_struct;
        struct msghdr msg;
        size_t msg_len;

        msg_buf_struct = (struct iovec){
            msg_buf,
            sizeof(msg_buf)
        };
        msg = (struct msghdr){
            NULL,
            0,
            &msg_buf_struct,
            1,
            packet_info_buf,
            sizeof(packet_info_buf),
            0
        };
#endif
        cmsghdr_t *cmsg;
        size_t ip_hdr_len;
        struct icmp *reply;
        int reply_id;
        int reply_seq;
        uint16_t reply_checksum;
        uint16_t checksum;

#ifdef _WIN32
        error = WSARecvMsg(ctx->sockfd, &msg, &msg_len, NULL, NULL);
#else
        error = (int)recvmsg(ctx->sockfd, &msg, 0);
#endif

        delay = get_utime() - start_time;

        if (error < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
            if (errno == EAGAIN) {
#endif
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

#ifndef _WIN32
        msg_len = error;
#endif

        if (ctx->addr.ss_family == AF_INET6) {
            ip_hdr_len = 0;

#ifndef __sun__
            for (
                cmsg = CMSG_FIRSTHDR(&msg);
                cmsg != NULL;
                cmsg = CMSG_NXTHDR(&msg, cmsg))
            {
                if (cmsg->cmsg_level == IPPROTO_IPV6
                    && cmsg->cmsg_type == IPV6_PKTINFO) {
                    struct in6_pktinfo *pktinfo = (void *)CMSG_DATA(cmsg);
                    memcpy(&msg_addr,
                           &pktinfo->ipi6_addr,
                           sizeof(struct in6_addr));
                }
            }
#endif
        } else {
            ip_hdr_len = ((*(uint8_t *)msg_buf) & 0x0F) * 4;
        }

        reply = (struct icmp *)(msg_buf + ip_hdr_len);
        reply_id = ntohs(reply->icmp_id);
        reply_seq = ntohs(reply->icmp_seq);

        if (!(ctx->addr.ss_family == AF_INET
              && reply->icmp_type == ICMP_ECHO_REPLY)
            && !(ctx->addr.ss_family == AF_INET6
                 && reply->icmp_type == ICMP6_ECHO_REPLY)) {
            continue;
        }

        if (reply_id != ctx->id || reply_seq != (ctx->seq - 1)) {
            continue;
        }

        reply_checksum = reply->icmp_cksum;
        reply->icmp_cksum = 0;

        if (ctx->addr.ss_family == AF_INET6) {
            size_t size;
            struct icmp6_packet *reply_packet;

            size = sizeof(struct ip6_pseudo_hdr) + msg_len;
            reply_packet = calloc(1, size);

            if (reply_packet == NULL) {
                *ping_time_ms = -1.0;
                return 0;
            }

            memcpy(&reply_packet->ip6_hdr.src,
                   &((struct sockaddr_in6 *)&ctx->addr)->sin6_addr,
                   sizeof(struct in6_addr));
            reply_packet->ip6_hdr.dst = msg_addr;
            reply_packet->ip6_hdr.plen = htons((uint16_t)msg_len);
            reply_packet->ip6_hdr.nxt = IPPROTO_ICMPV6;
            memcpy(&reply_packet->icmp,
                   msg_buf + ip_hdr_len,
                   msg_len - ip_hdr_len);

            checksum = compute_checksum((char *)reply_packet, size);
            free(reply_packet);
        } else {
            checksum = compute_checksum(msg_buf + ip_hdr_len,
                                        msg_len - ip_hdr_len);
        }

        *ping_time_ms = (double)delay / 1000.0;
        return reply_checksum == checksum;
    }
}

void sryze_ping_destroy(sryze_ping_context_t *ctx)
{
    if (!ctx) return;

    if (ctx->sockfd >= 0) {
        close_socket(ctx->sockfd);
    }
    free(ctx);
}
