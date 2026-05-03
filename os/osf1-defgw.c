#include "os_interface.h"

#define _SOCKADDR_LEN 1
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DEFGW_ROUNDUP(a) \
    ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int os_get_default_gw_ip(char *buf, size_t buflen) {
    int s, pid, i;
    int seq = 1;
    ssize_t n;
    struct {
        struct rt_msghdr hdr;
        struct sockaddr_in dst;
        struct sockaddr_in mask;
    } req;
    char rbuf[2048];
    struct rt_msghdr *rtm;
    struct sockaddr *sa, *gw;
    const char *str;

    if (!buf || buflen < 16) return 0;

    s = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (s < 0) return 0;

    pid = getpid();
    memset(&req, 0, sizeof(req));
    req.hdr.rtm_msglen = sizeof(req);
    req.hdr.rtm_version = RTM_VERSION;
    req.hdr.rtm_type = RTM_GET;
    req.hdr.rtm_addrs = RTA_DST | RTA_NETMASK;
    req.hdr.rtm_pid = pid;
    req.hdr.rtm_seq = seq;

    req.dst.sin_len = sizeof(struct sockaddr_in);
    req.dst.sin_family = AF_INET;
    req.dst.sin_addr.s_addr = INADDR_ANY;

    req.mask.sin_len = sizeof(struct sockaddr_in);
    req.mask.sin_family = AF_INET;
    req.mask.sin_addr.s_addr = INADDR_ANY;

    if (write(s, &req, sizeof(req)) < 0) { close(s); return 0; }

    for (;;) {
        n = read(s, rbuf, sizeof(rbuf));
        if (n <= 0) { close(s); return 0; }
        rtm = (struct rt_msghdr *)rbuf;
        if (rtm->rtm_version != RTM_VERSION) continue;
        if (rtm->rtm_type == RTM_GET && rtm->rtm_seq == seq && rtm->rtm_pid == pid) break;
    }
    close(s);

    if (!(rtm->rtm_addrs & RTA_GATEWAY)) return 0;

    sa = (struct sockaddr *)(rtm + 1);
    gw = NULL;
    for (i = 0; i < 8; i++) {
        if (!(rtm->rtm_addrs & (1 << i))) continue;
        if (i == 1) { gw = sa; break; }
        sa = (struct sockaddr *)((char *)sa + DEFGW_ROUNDUP(sa->sa_len));
    }

    if (!gw || gw->sa_family != AF_INET) return 0;

    str = inet_ntoa(((struct sockaddr_in *)gw)->sin_addr);
    if (!str) return 0;
    snprintf(buf, buflen, "%s", str);
    return 1;
}
