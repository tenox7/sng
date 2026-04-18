/*
 * Default gateway discovery.
 *
 * Approach per platform (borrowed from net-snmp):
 *   Linux:        parse /proc/net/route (dst == 0 is the default route)
 *   Darwin/BSD:   sysctl NET_RT_DUMP over PF_ROUTE, find RTF_GATEWAY
 *                 entry with zero destination
 *   Legacy Unix:  popen("netstat -rn") and parse the "default" row
 */

#include "../compat.h"
#include "os_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)

int os_get_default_gateway(char *buf, size_t buflen) {
    FILE *fp;
    char line[256];
    char iface[64];
    unsigned long dest, gw, flags, refcnt, use, metric, mask;
    int got;

    if (!buf || buflen < 16) return 0;

    fp = fopen("/proc/net/route", "r");
    if (!fp) return 0;

    /* skip header */
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }

    got = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63s %lx %lx %lx %lu %lu %lu %lx",
                   iface, &dest, &gw, &flags, &refcnt, &use, &metric, &mask) >= 3) {
            if (dest == 0) {
                unsigned int b0 = (unsigned int)((gw >>  0) & 0xff);
                unsigned int b1 = (unsigned int)((gw >>  8) & 0xff);
                unsigned int b2 = (unsigned int)((gw >> 16) & 0xff);
                unsigned int b3 = (unsigned int)((gw >> 24) & 0xff);
                snprintf(buf, buflen, "%u.%u.%u.%u", b0, b1, b2, b3);
                got = 1;
                break;
            }
        }
    }
    fclose(fp);
    return got;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef SA_SIZE
#define SA_SIZE(sa)                                             \
    ((!(sa) || ((struct sockaddr *)(sa))->sa_len == 0) ?        \
        sizeof(long) :                                          \
        1 + ((((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1)))
#endif

int os_get_default_gateway(char *buf, size_t buflen) {
    int mib[6];
    size_t len;
    char *rtbuf, *next, *lim;
    struct rt_msghdr *rtm;
    struct sockaddr *sa, *dst, *gw;
    int i;
    int got = 0;

    if (!buf || buflen < 16) return 0;

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = AF_INET;
    mib[4] = NET_RT_DUMP;
    mib[5] = 0;

    if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) return 0;
    rtbuf = (char *)malloc(len);
    if (!rtbuf) return 0;
    if (sysctl(mib, 6, rtbuf, &len, NULL, 0) < 0) {
        free(rtbuf);
        return 0;
    }

    lim = rtbuf + len;
    for (next = rtbuf; next < lim; next += rtm->rtm_msglen) {
        rtm = (struct rt_msghdr *)next;
        if (rtm->rtm_version != RTM_VERSION) continue;
        if (!(rtm->rtm_flags & RTF_GATEWAY)) continue;
        if (!(rtm->rtm_addrs & RTA_DST) || !(rtm->rtm_addrs & RTA_GATEWAY)) continue;

        sa = (struct sockaddr *)(rtm + 1);
        dst = NULL;
        gw = NULL;
        for (i = 0; i < RTAX_MAX; i++) {
            if (rtm->rtm_addrs & (1 << i)) {
                if (i == RTAX_DST) dst = sa;
                else if (i == RTAX_GATEWAY) gw = sa;
                sa = (struct sockaddr *)((char *)sa + SA_SIZE(sa));
            }
        }

        if (dst && dst->sa_family == AF_INET && gw && gw->sa_family == AF_INET) {
            struct sockaddr_in *d = (struct sockaddr_in *)dst;
            struct sockaddr_in *g = (struct sockaddr_in *)gw;
            if (d->sin_addr.s_addr == INADDR_ANY) {
                const char *s = inet_ntoa(g->sin_addr);
                if (s) {
                    snprintf(buf, buflen, "%s", s);
                    got = 1;
                    break;
                }
            }
        }
    }

    free(rtbuf);
    return got;
}

#else

/* Generic fallback: parse output of `netstat -rn`.
 * Works on Solaris, AIX, HP-UX, IRIX, Tru64, UnixWare, and anything else
 * with a BSD-flavored netstat. */

static int looks_like_ipv4(const char *s) {
    int dots = 0;
    int digits = 0;
    while (*s) {
        if (*s == '.') { dots++; digits = 0; }
        else if (*s >= '0' && *s <= '9') { digits++; if (digits > 3) return 0; }
        else return 0;
        s++;
    }
    return dots == 3;
}

int os_get_default_gateway(char *buf, size_t buflen) {
    FILE *fp;
    char line[512];
    char dest[128], gw[128];
    int got = 0;

    if (!buf || buflen < 16) return 0;

    fp = popen("netstat -rn 2>/dev/null", "r");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%127s %127s", dest, gw) == 2) {
            if ((strcmp(dest, "default") == 0 ||
                 strcmp(dest, "0.0.0.0") == 0 ||
                 strcmp(dest, "0.0.0.0/0") == 0) &&
                looks_like_ipv4(gw)) {
                snprintf(buf, buflen, "%s", gw);
                got = 1;
                break;
            }
        }
    }
    pclose(fp);
    return got;
}

#endif
