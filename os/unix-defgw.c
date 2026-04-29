#include "os_interface.h"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) || defined(__DragonFly__) || \
    defined(__sgi) || defined(sgi) || \
    defined(__osf__) || defined(__digital__)

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef SA_SIZE
#define SA_SIZE(sa)                                             \
    ((!(sa) || ((struct sockaddr *)(sa))->sa_len == 0) ?        \
        sizeof(long) :                                          \
        1 + ((((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1)))
#endif

int os_get_default_gw_ip(char *buf, size_t buflen) {
    int mib[6];
    size_t len;
    char *rtbuf, *next, *lim;
    struct rt_msghdr *rtm;
    struct sockaddr *sa, *dst, *gw;
    struct sockaddr_in *d, *g;
    const char *s;
    int i;

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
    if (sysctl(mib, 6, rtbuf, &len, NULL, 0) < 0) { free(rtbuf); return 0; }

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
            if (!(rtm->rtm_addrs & (1 << i))) continue;
            if (i == RTAX_DST) dst = sa;
            else if (i == RTAX_GATEWAY) gw = sa;
            sa = (struct sockaddr *)((char *)sa + SA_SIZE(sa));
        }

        if (!dst || dst->sa_family != AF_INET) continue;
        if (!gw || gw->sa_family != AF_INET) continue;

        d = (struct sockaddr_in *)dst;
        g = (struct sockaddr_in *)gw;
        if (d->sin_addr.s_addr != INADDR_ANY) continue;

        s = inet_ntoa(g->sin_addr);
        if (!s) continue;
        snprintf(buf, buflen, "%s", s);
        free(rtbuf);
        return 1;
    }
    free(rtbuf);
    return 0;
}

#else

#include <stdio.h>
#include <string.h>

static int defgw_looks_like_ipv4(const char *s) {
    int dots;
    int digits;

    dots = 0;
    digits = 0;
    while (*s) {
        if (*s == '.') { dots++; digits = 0; }
        else if (*s >= '0' && *s <= '9') { digits++; if (digits > 3) return 0; }
        else return 0;
        s++;
    }
    return dots == 3;
}

int os_get_default_gw_ip(char *buf, size_t buflen) {
    FILE *fp;
    char line[512];
    char dest[128], gw[128];

    if (!buf || buflen < 16) return 0;

    fp = popen("netstat -rn 2>/dev/null", "r");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%127s %127s", dest, gw) != 2) continue;
        if (strcmp(dest, "default") != 0 &&
            strcmp(dest, "0.0.0.0") != 0 &&
            strcmp(dest, "0.0.0.0/0") != 0) continue;
        if (!defgw_looks_like_ipv4(gw)) continue;
        snprintf(buf, buflen, "%s", gw);
        pclose(fp);
        return 1;
    }
    pclose(fp);
    return 0;
}

#endif
