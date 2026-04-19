/*
 * Default gateway discovery.
 *
 * Uses the same platform-specific techniques as net-snmp, with
 * `netstat -rn` as a last-resort fallback on every platform if the
 * native path fails.
 *
 *   Linux                : parse /proc/net/route
 *   Darwin / *BSD / IRIX / Tru64
 *                        : sysctl CTL_NET,PF_ROUTE,0,AF_INET,NET_RT_DUMP,0
 *   Solaris              : STREAMS T_OPTMGMT_REQ on /dev/arp (MIB2_IP /
 *                          MIB2_IP_ROUTE) — net-snmp's getmib approach
 *   HP-UX 11             : open_mib("/dev/ip") + get_mib_info(ID_ipRouteTable)
 *                          from libnm — net-snmp's hpux11 path
 *   AIX / UnixWare / etc : netstat only (no native path)
 */

#include "../compat.h"
#include "os_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int try_netstat(char *buf, size_t buflen);

/* -------------------------------------------------------------------- */
#if defined(__linux__)

static int try_native(char *buf, size_t buflen) {
    FILE *fp;
    char line[256];
    char iface[64];
    unsigned long dest, gw, flags, refcnt, use, metric, mask;

    fp = fopen("/proc/net/route", "r");
    if (!fp) return 0;

    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63s %lx %lx %lx %lu %lu %lu %lx",
                   iface, &dest, &gw, &flags, &refcnt, &use, &metric, &mask) >= 3) {
            if (dest == 0) {
                unsigned int b0 = (unsigned int)((gw >>  0) & 0xff);
                unsigned int b1 = (unsigned int)((gw >>  8) & 0xff);
                unsigned int b2 = (unsigned int)((gw >> 16) & 0xff);
                unsigned int b3 = (unsigned int)((gw >> 24) & 0xff);
                snprintf(buf, buflen, "%u.%u.%u.%u", b0, b1, b2, b3);
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

/* -------------------------------------------------------------------- */
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
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

#ifndef SA_SIZE
#define SA_SIZE(sa)                                             \
    ((!(sa) || ((struct sockaddr *)(sa))->sa_len == 0) ?        \
        sizeof(long) :                                          \
        1 + ((((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1)))
#endif

static int try_native(char *buf, size_t buflen) {
    int mib[6];
    size_t len;
    char *rtbuf, *next, *lim;
    struct rt_msghdr *rtm;
    struct sockaddr *sa, *dst, *gw;
    int i;

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
        dst = NULL; gw = NULL;
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
                    free(rtbuf);
                    return 1;
                }
            }
        }
    }
    free(rtbuf);
    return 0;
}

/* -------------------------------------------------------------------- */
#elif defined(__sun) || defined(__sun__) || defined(sun)

/*
 * Solaris: STREAMS T_OPTMGMT_REQ on /dev/arp (or fall back to /dev/ip).
 * This is a minimal port of net-snmp's getmib() (agent/mibgroup/kernel_sunos5.c),
 * specialized for MIB2_IP / MIB2_IP_ROUTE.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <fcntl.h>
#include <unistd.h>
#include <inet/mib2.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int try_native(char *buf, size_t buflen) {
    int sd;
    struct strbuf ctlbuf, databuf;
    char ctl[2048];
    char *data;
    size_t data_cap;
    struct T_optmgmt_req *tor;
    struct T_optmgmt_ack *toa;
    struct opthdr *req;
    int flags, rc;
    int found = 0;

    sd = open("/dev/arp", O_RDWR);
    if (sd < 0) sd = open("/dev/ip", O_RDWR);
    if (sd < 0) return 0;

    if (ioctl(sd, I_PUSH, "tcp") < 0 || ioctl(sd, I_PUSH, "udp") < 0) {
        close(sd);
        return 0;
    }

    data_cap = 65536;
    data = (char *)malloc(data_cap);
    if (!data) { close(sd); return 0; }

    tor = (struct T_optmgmt_req *)ctl;
    tor->PRIM_type = T_OPTMGMT_REQ;
    tor->OPT_offset = sizeof(struct T_optmgmt_req);
    tor->OPT_length = sizeof(struct opthdr);
#ifdef T_CURRENT
    tor->MGMT_flags = T_CURRENT;
#else
    tor->MGMT_flags = MI_T_CURRENT;
#endif
    req = (struct opthdr *)(tor + 1);
    req->level = MIB2_IP;
    req->name  = 0;
    req->len   = 0;

    ctlbuf.buf    = ctl;
    ctlbuf.len    = tor->OPT_length + tor->OPT_offset;
    ctlbuf.maxlen = sizeof(ctl);
    if (putmsg(sd, &ctlbuf, NULL, 0) < 0) { free(data); close(sd); return 0; }

    toa = (struct T_optmgmt_ack *)ctl;
    req = (struct opthdr *)(toa + 1);

    for (;;) {
        flags = 0;
        ctlbuf.maxlen = sizeof(ctl);
        ctlbuf.len = 0;
        rc = getmsg(sd, &ctlbuf, NULL, &flags);
        if (rc < 0) break;

        if (rc == 0 &&
            ctlbuf.len >= (int)sizeof(struct T_optmgmt_ack) &&
            toa->PRIM_type == T_OPTMGMT_ACK &&
            toa->MGMT_flags == T_SUCCESS &&
            req->len == 0) {
            /* end of data */
            break;
        }

        /* Read data payload for this group/table */
        databuf.buf    = data;
        databuf.maxlen = data_cap;
        databuf.len    = 0;
        flags = 0;
        rc = getmsg(sd, NULL, &databuf, &flags);
        if (rc < 0) break;

        if (req->level == MIB2_IP && req->name == MIB2_IP_ROUTE) {
            mib2_ipRouteEntry_t *entries = (mib2_ipRouteEntry_t *)databuf.buf;
            size_t n = databuf.len / sizeof(mib2_ipRouteEntry_t);
            size_t i;
            for (i = 0; i < n; i++) {
                int is_default = (entries[i].ipRouteInfo.re_ire_type & IRE_DEFAULT) != 0;
                if (!is_default && entries[i].ipRouteDest == 0 && entries[i].ipRouteMask == 0) {
                    is_default = 1;
                }
                if (is_default) {
                    struct in_addr ia;
                    const char *s;
                    ia.s_addr = entries[i].ipRouteNextHop;
                    s = inet_ntoa(ia);
                    if (s && *s && strcmp(s, "0.0.0.0") != 0) {
                        snprintf(buf, buflen, "%s", s);
                        found = 1;
                        break;
                    }
                }
            }
            if (found) break;
        }
        /* drain any remaining MOREDATA for this table */
        while (rc == MOREDATA) {
            databuf.len = 0;
            flags = 0;
            rc = getmsg(sd, NULL, &databuf, &flags);
            if (rc < 0) break;
        }
    }

    ioctl(sd, I_FLUSH, FLUSHRW);
    close(sd);
    free(data);
    return found;
}

/* -------------------------------------------------------------------- */
#elif defined(__hpux) || defined(hpux) || defined(_HPUX_SOURCE)

/*
 * HP-UX 11: open_mib / get_mib_info (libnm, already linked via -lnm).
 * Follows net-snmp/agent/mibgroup/mibII/var_route.c hpux11 path.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/mib.h>
#include <netinet/mib_kern.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int try_native(char *buf, size_t buflen) {
    int fd, count, i;
    struct nmparms p;
    unsigned int ulen;
    int val;
    mib_ipRouteEnt *table;
    int found = 0;

    fd = open_mib("/dev/ip", O_RDONLY, 0, NM_ASYNC_OFF);
    if (fd < 0) return 0;

    p.objid  = ID_ipRouteNumEnt;
    p.buffer = (void *)&val;
    ulen     = sizeof(val);
    p.len    = &ulen;
    if (get_mib_info(fd, &p) != 0 || val <= 0) { close_mib(fd); return 0; }
    count = val;

    table = (mib_ipRouteEnt *)malloc(count * sizeof(mib_ipRouteEnt));
    if (!table) { close_mib(fd); return 0; }

    p.objid  = ID_ipRouteTable;
    p.buffer = (void *)table;
    ulen     = count * sizeof(mib_ipRouteEnt);
    p.len    = &ulen;
    if (get_mib_info(fd, &p) != 0) { free(table); close_mib(fd); return 0; }

    for (i = 0; i < count; i++) {
        if (table[i].Dest == 0 && table[i].Mask == 0) {
            struct in_addr ia;
            const char *s;
            ia.s_addr = table[i].NextHop;
            s = inet_ntoa(ia);
            if (s && *s && strcmp(s, "0.0.0.0") != 0) {
                snprintf(buf, buflen, "%s", s);
                found = 1;
                break;
            }
        }
    }

    free(table);
    close_mib(fd);
    return found;
}

/* -------------------------------------------------------------------- */
#else

/* AIX, UnixWare, and anything else unknown: no native path. */
#define NO_NATIVE_GW 1

#endif

/* -------------------------------------------------------------------- */
/* Universal fallback: parse `netstat -rn` output */

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

static int try_netstat(char *buf, size_t buflen) {
    FILE *fp;
    char line[512];
    char dest[128], gw[128];

    fp = popen("netstat -rn 2>/dev/null", "r");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%127s %127s", dest, gw) == 2) {
            if ((strcmp(dest, "default") == 0 ||
                 strcmp(dest, "0.0.0.0") == 0 ||
                 strcmp(dest, "0.0.0.0/0") == 0) &&
                looks_like_ipv4(gw)) {
                snprintf(buf, buflen, "%s", gw);
                pclose(fp);
                return 1;
            }
        }
    }
    pclose(fp);
    return 0;
}

/* -------------------------------------------------------------------- */

int os_get_default_gateway(char *buf, size_t buflen) {
    if (!buf || buflen < 16) return 0;
#ifndef NO_NATIVE_GW
    if (try_native(buf, buflen)) return 1;
#endif
    return try_netstat(buf, buflen);
}
