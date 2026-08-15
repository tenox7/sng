/*
 * SCO OpenServer 6 (SVR5 kernel, "SCO_SV 5 6.0.0").
 *
 * Unlike the UnixWare 7 backend this one has POSIX threads (libthread, cc
 * -Kthread), so it shares posix_timer.c with the modern platforms.
 *
 * Statistics come from three places:
 *   - CPU and free memory: the MAS kernel metric file (/var/adm/metreg.data)
 *   - swap: swapctl(SC_LIST)
 *   - interface counters: DLPI DL_GET_STATISTICS_REQ on /dev/<ifname>, which
 *     is what netstat(1M) does; the IP layer's own ifstats only has octets
 *     for loopback.
 */

#include "os_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <math.h>
#include <time.h>
#include <stropts.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/ioctl.h>
#include <sys/dl.h>
#include <sys/swap.h>
#include <sys/dlpi.h>
#include <sys/scodlpi.h>
#include <sys/procfs.h>
#include <net/route.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mas.h>
#include <metreg.h>

#define CPUSTATES 4

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};

static int mas_fd = -1;
static int mas_initialized = 0;
static uint32_t ncpu = 0;
static uint32_t cp_old[CPUSTATES];
static long total_mem_pages = 0;

static long mas_freemem_pages(void);

const char* os_get_platform_name(void) {
    return "openserver6";
}

int os_init(void) {
    uint32_t *ncpu_p;
    int i;

    if (mas_initialized)
        return 1;

    total_mem_pages = sysconf(_SC_TOTAL_MEMORY);

    mas_fd = mas_open(MAS_FILE, MAS_MMAP_ACCESS);
    if (mas_fd < 0)
        return 0;

    ncpu_p = (uint32_t *)mas_get_met(mas_fd, NCPU, 0);
    if (!ncpu_p) {
        mas_close(mas_fd);
        mas_fd = -1;
        return 0;
    }
    ncpu = (uint32_t)(*(short *)ncpu_p);

    for (i = 0; i < CPUSTATES; i++)
        cp_old[i] = 0;

    mas_initialized = 1;
    mas_freemem_pages();    /* take the baseline the free memory rate needs */
    return 1;
}

void os_cleanup(void) {
    if (mas_fd >= 0) {
        mas_close(mas_fd);
        mas_fd = -1;
    }
    mas_initialized = 0;
}

static uint32_t get_cpu_metric(metid_t type) {
    uint32_t total;
    uint32_t *p;
    uint32_t i;

    total = 0;
    for (i = 0; i < ncpu; i++) {
        p = (uint32_t *)mas_get_met(mas_fd, type, i);
        if (p)
            total += *p;
    }
    return total;
}

static void calculate_cpu_percentages(uint32_t *new_vals, double *percentages) {
    uint32_t total_change;
    uint32_t half_total;
    int i;

    total_change = 0;
    for (i = 0; i < CPUSTATES; i++)
        total_change += (new_vals[i] - cp_old[i]);

    if (total_change == 0) {
        for (i = 0; i < CPUSTATES; i++)
            percentages[i] = 0.0;
        return;
    }

    half_total = total_change / 2;
    for (i = 0; i < CPUSTATES; i++) {
        percentages[i] = ((new_vals[i] - cp_old[i]) * 1000 + half_total) / total_change;
        percentages[i] = percentages[i] / 10.0;
        cp_old[i] = new_vals[i];
    }
}

static int read_cpu_states(double *percentages) {
    uint32_t cpu_states[CPUSTATES];

    if (!mas_initialized || mas_fd < 0)
        return 0;

    cpu_states[0] = get_cpu_metric(MPC_CPU_IDLE);
    cpu_states[1] = get_cpu_metric(MPC_CPU_USR);
    cpu_states[2] = get_cpu_metric(MPC_CPU_SYS);
    cpu_states[3] = get_cpu_metric(MPC_CPU_WIO);

    calculate_cpu_percentages(cpu_states, percentages);
    return 1;
}

int os_cpu_get_stats(double *value) {
    double percentages[CPUSTATES];

    if (!read_cpu_states(percentages)) {
        *value = 0.0;
        return 0;
    }

    *value = 100.0 - percentages[0];
    OS_CLAMP_PCT(*value);
    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    double percentages[CPUSTATES];

    if (!read_cpu_states(percentages)) {
        *total_value = 0.0;
        *system_value = 0.0;
        return 0;
    }

    *total_value = 100.0 - percentages[0];
    *system_value = percentages[2];
    OS_CLAMP_PCT(*total_value);
    OS_CLAMP_PCT(*system_value);
    return 1;
}

/* freemem is a dl_t accumulator: the kernel adds the current free page count
 * to it once a second, so the instantaneous value is the difference between
 * two samples divided by the seconds between them (this is what sar -r and
 * top do). Returns -1 until a second sample is available. */
static long mas_freemem_pages(void) {
    static dl_t prev;
    static time_t prev_time = 0;
    dl_t *cur, diff, den, quot;
    time_t now;
    long secs;

    if (!mas_initialized || mas_fd < 0)
        return -1;

    cur = (dl_t *)mas_get_met(mas_fd, FREEMEM, 0);
    if (!cur)
        return -1;

    now = time(NULL);
    if (prev_time == 0) {
        prev = *cur;
        prev_time = now;
        return -1;
    }

    /* too soon to divide - keep the baseline rather than resetting it */
    secs = (long)(now - prev_time);
    if (secs < 1)
        return -1;

    diff = lsub(*cur, prev);
    den.dl_lop = (ulong_t)secs;
    den.dl_hop = 0;
    quot = ldivide(diff, den);

    prev = *cur;
    prev_time = now;
    return (long)quot.dl_lop;
}

static int swap_usage(long *total, long *used) {
    swaptbl_t *swt;
    char *paths;
    int n, i;

    *total = 0;
    *used = 0;

    n = swapctl(SC_GETNSWP, 0);
    if (n <= 0)
        return n == 0;

    swt = (swaptbl_t *)malloc(sizeof(int) + n * sizeof(swapent_t));
    if (!swt)
        return 0;
    paths = (char *)malloc(n * MAXPATHLEN);
    if (!paths) {
        free(swt);
        return 0;
    }

    swt->swt_n = n;
    for (i = 0; i < n; i++)
        swt->swt_ent[i].ste_path = paths + i * MAXPATHLEN;

    if (swapctl(SC_LIST, swt) < 0) {
        free(paths);
        free(swt);
        return 0;
    }

    for (i = 0; i < swt->swt_n; i++) {
        *total += swt->swt_ent[i].ste_pages;
        *used += swt->swt_ent[i].ste_pages - swt->swt_ent[i].ste_free;
    }

    free(paths);
    free(swt);
    return 1;
}

/* The kernel registers freefilemem but keeps it identical to freemem on this
 * release, so the file cache cannot be separated out here: used memory is
 * simply physical minus free. */
int os_memory_get_stats_dual(double *used_value, double *swap_value) {
    long free_pages, swap_total, swap_used;

    if (swap_value) {
        *swap_value = 0.0;
        if (swap_usage(&swap_total, &swap_used) && swap_total > 0) {
            *swap_value = (double)swap_used * 100.0 / (double)swap_total;
            OS_CLAMP_PCT(*swap_value);
        }
    }

    if (!used_value)
        return 1;

    *used_value = 0.0;
    free_pages = mas_freemem_pages();
    if (free_pages < 0 || total_mem_pages <= 0)
        return 0;

    *used_value = (double)(total_mem_pages - free_pages) * 100.0 / (double)total_mem_pages;
    OS_CLAMP_PCT(*used_value);
    return 1;
}

int os_memory_get_stats(double *value) {
    double swap;
    return os_memory_get_stats_dual(value, &swap);
}

/* The kernel exports neither avenrun (the symbol exists but stays zero, which
 * is why uptime(1) always prints 0.00) nor a moving runque metric, so the
 * 1-minute average is accumulated here from run-queue samples taken out of
 * /proc. */
static int count_runnable(void) {
    DIR *dir;
    struct dirent *ent;
    psinfo_t ps;
    char path[MAXPATHLEN];
    int fd, run;

    dir = opendir("/proc");
    if (!dir)
        return -1;

    run = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9')
            continue;
        snprintf(path, sizeof(path), "/proc/%s/psinfo", ent->d_name);
        fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        if (read(fd, &ps, sizeof(ps)) == sizeof(ps) &&
            (ps.pr_lwp.pr_sname == 'R' || ps.pr_lwp.pr_sname == 'O'))
            run++;
        close(fd);
    }
    closedir(dir);

    /* discount ourselves: we are always on-proc while scanning */
    return run > 0 ? run - 1 : 0;
}

int os_loadavg_get_stats(double *value) {
    static double load = -1.0;
    static time_t prev_time = 0;
    double sample, weight;
    time_t now;
    long secs;
    int run;

    *value = 0.0;
    run = count_runnable();
    if (run < 0)
        return 0;

    sample = (double)run;
    now = time(NULL);

    if (load < 0.0) {
        load = sample;
    } else {
        secs = (long)(now - prev_time);
        if (secs < 1) secs = 1;
        weight = exp(-(double)secs / 60.0);
        load = load * weight + sample * (1.0 - weight);
    }
    prev_time = now;

    *value = load;
    return 1;
}

/* MDI drivers keep the MIB-II octet counters, reachable with a single DLPI
 * DL_GET_STATISTICS_REQ on the interface's device node. */
static int dlpi_if_stats(const char *name, uint32_t *in_bytes, uint32_t *out_bytes) {
    char devpath[MAXPATHLEN];
    char ctl[sizeof(dl_get_statistics_ack_t) + sizeof(struct dlpi_stats) + 256];
    struct strbuf cbuf;
    dl_get_statistics_req_t req;
    dl_get_statistics_ack_t *ack;
    struct dlpi_stats *st;
    int fd, flags;

    if (strchr(name, '/'))
        return 0;
    snprintf(devpath, sizeof(devpath), "/dev/%s", name);

    fd = open(devpath, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return 0;

    req.dl_primitive = DL_GET_STATISTICS_REQ;
    cbuf.buf = (char *)&req;
    cbuf.len = sizeof(req);
    cbuf.maxlen = sizeof(req);
    if (putmsg(fd, &cbuf, NULL, RS_HIPRI) < 0) {
        close(fd);
        return 0;
    }

    cbuf.buf = ctl;
    cbuf.len = 0;
    cbuf.maxlen = sizeof(ctl);
    flags = 0;
    if (getmsg(fd, &cbuf, NULL, &flags) < 0) {
        close(fd);
        return 0;
    }
    close(fd);

    ack = (dl_get_statistics_ack_t *)ctl;
    if ((int)cbuf.len < (int)sizeof(*ack) ||
        ack->dl_primitive != DL_GET_STATISTICS_ACK ||
        ack->dl_stat_offset + ack->dl_stat_length > (ulong)cbuf.len ||
        ack->dl_stat_length < sizeof(struct dlpi_stats))
        return 0;

    st = (struct dlpi_stats *)(ctl + ack->dl_stat_offset);
    *in_bytes = (uint32_t)st->mac_rx.mac_octets;
    *out_bytes = (uint32_t)st->mac_tx.mac_octets;
    return 1;
}

/* Loopback has no DLPI device; its octets live in the IP layer's ifstats. */
static int ip_if_stats(int sock, u_long index, char *name, size_t namelen,
                       uint32_t *in_bytes, uint32_t *out_bytes, u_long *found_index) {
    struct ifreq_all ia;

    memset(&ia, 0, sizeof(ia));
    ia.if_entry.if_index = index;
    if (ioctl(sock, SIOCGIFALL, &ia) < 0)
        return 0;

    *found_index = ia.if_entry.if_index;
    if (name) {
        strncpy(name, ia.if_entry.if_name, namelen - 1);
        name[namelen - 1] = '\0';
    }
    if (in_bytes) *in_bytes = (uint32_t)ia.if_stats.ifinoctets;
    if (out_bytes) *out_bytes = (uint32_t)ia.if_stats.ifoutoctets;
    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    char name[IFNAMSIZ + 1];
    uint32_t rx, tx, sum_in, sum_out;
    u_long index, found;
    int sock, all, found_any;

    if (!interface_name || !in_bytes || !out_bytes)
        return 0;

    all = IF_IS_ALL(interface_name);

    if (!all && !IF_IS_LOOPBACK(interface_name))
        return dlpi_if_stats(interface_name, in_bytes, out_bytes);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return 0;

    sum_in = 0;
    sum_out = 0;
    found_any = 0;

    /* SIOCGIFALL returns the first interface whose SNMP index is >= the one
     * asked for, so walking is "ask for last + 1" until the ioctl fails. */
    for (index = 1; index < 4096; index = found + 1) {
        if (!ip_if_stats(sock, index, name, sizeof(name), &rx, &tx, &found))
            break;
        if (found < index)      /* never walked backwards in practice */
            break;

        if (!all) {
            if (strcmp(name, interface_name) != 0)
                continue;
            *in_bytes = rx;
            *out_bytes = tx;
            close(sock);
            return 1;
        }

        if (IF_IS_LOOPBACK(name))
            continue;
        if (!dlpi_if_stats(name, &rx, &tx))
            continue;
        sum_in += rx;
        sum_out += tx;
        found_any = 1;
    }

    close(sock);
    if (!found_any)
        return 0;

    *in_bytes = sum_in;
    *out_bytes = sum_out;
    return 1;
}

int os_get_default_gw_ip(char *buf, size_t buflen) {
    struct rt_giarg gi;
    struct rt_msghdr *rtm;
    struct sockaddr *sa, *dst, *gw;
    struct sockaddr_in *d, *g;
    char *rtbuf, *next, *lim;
    const char *s;
    int fd, i;

    if (!buf || buflen < 16) return 0;

    fd = open("/dev/route", O_RDONLY);
    if (fd < 0) return 0;

    gi.gi_op = KINFO_RT_DUMP;
    gi.gi_where = 0;
    gi.gi_size = 0;
    gi.gi_arg = 0;
    if (ioctl(fd, RTSTR_GETROUTE, &gi) < 0) { close(fd); return 0; }

    rtbuf = (char *)malloc(gi.gi_size);
    if (!rtbuf) { close(fd); return 0; }

    ((struct rt_giarg *)rtbuf)->gi_size = gi.gi_size;
    ((struct rt_giarg *)rtbuf)->gi_op = KINFO_RT_DUMP;
    ((struct rt_giarg *)rtbuf)->gi_where = (caddr_t)rtbuf;
    ((struct rt_giarg *)rtbuf)->gi_arg = 0;
    if (ioctl(fd, RTSTR_GETROUTE, rtbuf) < 0) { free(rtbuf); close(fd); return 0; }
    close(fd);

    lim = rtbuf + ((struct rt_giarg *)rtbuf)->gi_size;
    next = rtbuf + sizeof(struct rt_giarg);

    for (; next < lim; next += rtm->rtm_msglen) {
        rtm = (struct rt_msghdr *)next;
        if (rtm->rtm_version != RTM_VERSION) continue;
        if (!(rtm->rtm_flags & RTF_GATEWAY)) continue;
        if (!(rtm->rtm_addrs & RTA_DST) || !(rtm->rtm_addrs & RTA_GATEWAY)) continue;

        sa = (struct sockaddr *)(rtm + 1);
        dst = gw = NULL;
        for (i = 0; i < RTAX_MAX; i++) {
            if (!(rtm->rtm_addrs & (1 << i))) continue;
            if (i == RTAX_DST) dst = sa;
            else if (i == RTAX_GATEWAY) gw = sa;
            sa = (struct sockaddr *)((char *)sa + sizeof(struct sockaddr));
        }

        if (!dst || dst->sa_family != AF_INET) continue;
        if (!gw || gw->sa_family != AF_INET) continue;
        d = (struct sockaddr_in *)dst;
        g = (struct sockaddr_in *)gw;
        if (d->sin_addr.s_addr != INADDR_ANY) continue;

        s = inet_ntoa(g->sin_addr);
        if (!s) { free(rtbuf); return 0; }
        snprintf(buf, buflen, "%s", s);
        free(rtbuf);
        return 1;
    }
    free(rtbuf);
    return 0;
}

void os_sleep(uint32_t milliseconds) {
    struct timeval tv;

    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    select(0, NULL, NULL, NULL, &tv);
}

uint32_t os_get_time_ms(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

plot_mutex_t *os_plot_mutex_create(void) {
    plot_mutex_t *mutex;

    mutex = malloc(sizeof(plot_mutex_t));
    if (!mutex) return NULL;

    mutex->handle = malloc(sizeof(pthread_mutex_t));
    if (!mutex->handle) {
        free(mutex);
        return NULL;
    }

    if (pthread_mutex_init((pthread_mutex_t *)mutex->handle, NULL) != 0) {
        free(mutex->handle);
        free(mutex);
        return NULL;
    }

    return mutex;
}

void os_plot_mutex_destroy(plot_mutex_t *mutex) {
    if (!mutex) return;

    pthread_mutex_destroy((pthread_mutex_t *)mutex->handle);
    free(mutex->handle);
    free(mutex);
}

void os_plot_mutex_lock(plot_mutex_t *mutex) {
    if (!mutex) return;

    pthread_mutex_lock((pthread_mutex_t *)mutex->handle);
}

void os_plot_mutex_unlock(plot_mutex_t *mutex) {
    if (!mutex) return;

    pthread_mutex_unlock((pthread_mutex_t *)mutex->handle);
}

plot_thread_t *os_plot_thread_create(void (*func)(void *), void *arg) {
    plot_thread_t *thread;

    thread = malloc(sizeof(plot_thread_t));
    if (!thread) return NULL;

    thread->handle = malloc(sizeof(pthread_t));
    if (!thread->handle) {
        free(thread);
        return NULL;
    }

    if (pthread_create((pthread_t *)thread->handle, NULL,
                       (void *(*)(void *))func, arg) != 0) {
        free(thread->handle);
        free(thread);
        return NULL;
    }

    return thread;
}

void os_plot_thread_destroy(plot_thread_t *thread) {
    if (!thread) return;

    free(thread->handle);
    free(thread);
}

void os_plot_thread_join(plot_thread_t *thread) {
    if (!thread) return;

    pthread_join(*(pthread_t *)thread->handle, NULL);
}

int os_plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms) {
    if (!thread) return 0;

    return pthread_join(*(pthread_t *)thread->handle, NULL) == 0;
}

char *os_get_config_path(const char *filename) {
    return (char *)filename;
}

#include "unix-ping.c"
#include "posix_timer.c"
