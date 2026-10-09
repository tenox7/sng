/*
 * Reliant UNIX 5.45 (SINIX-N, MIPS).
 *
 * The kernel is LP64 while gcc builds o32 programs, so nothing is read from
 * /dev/kmem. Every statistic comes from statis() in libext, the call sadc and
 * uptime use. The record layouts are the kernel's, checked against netstat -i,
 * netstat -rn and uptime.
 *
 * Threads are the UNIX98 libpthread from /opt/pthread; Makefile.x11 has the
 * link line and the ELF flag it needs.
 */

#include "os_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/swap.h>
#include <sys/statis.h>
#include <sys/sysinfo.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CPUSTATES 5         /* idle user kernel wait sxbrk */
#define MAX_CPUS 64
#define IF_REC 84           /* netstats: name[16] ... in octets +44, out +48 */
#define RT_REC 72           /* routes: dst +0, gateway +16, flags +32 */
#define MAX_RECS 64
#define LOAD_SCALE 200.0    /* what /usr/ucb/uptime divides avenrun by */

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};

static uint32_t cp_old[CPUSTATES];

const char* os_get_platform_name(void) {
    return "reliant";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

static int statis_get(const char *fac, void *buf, size_t len) {
    memset(buf, 0, len);
    return statis(fac, STATIS_GET, buf, len) >= 0;
}

static int read_cpu_states(uint32_t *states) {
    unsigned char *buf;
    uint32_t *rec;
    int recsz = 0, ncpu, n, i, j;

    if (statis("sysinfo", STATIS_SIZ, &recsz, sizeof(recsz)) < 0 || recsz <= 0)
        return 0;

    buf = malloc((size_t)recsz * MAX_CPUS);
    if (!buf)
        return 0;

    memset(buf, 0, (size_t)recsz * MAX_CPUS);
    n = statis("sysinfo", STATIS_GET, buf, (size_t)recsz * MAX_CPUS);
    if (n <= 0) {
        free(buf);
        return 0;
    }

    memset(states, 0, CPUSTATES * sizeof(uint32_t));
    ncpu = n / recsz;
    for (i = 0; i < ncpu; i++) {
        rec = (uint32_t *)(buf + i * recsz);
        for (j = 0; j < CPUSTATES; j++)
            states[j] += rec[j];
    }

    free(buf);
    return 1;
}

static int cpu_percentages(double *pct) {
    uint32_t now[CPUSTATES];
    uint32_t total = 0;
    int i;

    if (!read_cpu_states(now))
        return 0;

    for (i = 0; i < CPUSTATES; i++)
        total += now[i] - cp_old[i];

    for (i = 0; i < CPUSTATES; i++) {
        pct[i] = total ? (double)(now[i] - cp_old[i]) * 100.0 / total : 0.0;
        cp_old[i] = now[i];
    }
    return 1;
}

int os_cpu_get_stats(double *value) {
    double pct[CPUSTATES];

    *value = 0.0;
    if (!cpu_percentages(pct))
        return 0;

    *value = 100.0 - pct[CPU_IDLE];
    OS_CLAMP_PCT(*value);
    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    double pct[CPUSTATES];

    *total_value = 0.0;
    *system_value = 0.0;
    if (!cpu_percentages(pct))
        return 0;

    *total_value = 100.0 - pct[CPU_IDLE];
    *system_value = pct[CPU_KERNEL];
    OS_CLAMP_PCT(*total_value);
    OS_CLAMP_PCT(*system_value);
    return 1;
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

/* physmem and the "vm total" fields are 64-bit; the low word is enough. t_free
 * is the last of them, a 60 second average that already includes the
 * reclaimable page cache. */
int os_memory_get_stats_dual(double *used_value, double *swap_value) {
    uint32_t phys[2], vmt[24];
    uint32_t total, free_pages;
    long swap_total, swap_used;

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
    if (!statis_get("physmem", phys, sizeof(phys)) || !statis_get("vm total", vmt, sizeof(vmt)))
        return 0;

    total = phys[1];
    free_pages = vmt[23];
    if (total == 0 || free_pages > total)
        return 0;

    *used_value = (double)(total - free_pages) * 100.0 / (double)total;
    OS_CLAMP_PCT(*used_value);
    return 1;
}

int os_memory_get_stats(double *value) {
    double swap;
    return os_memory_get_stats_dual(value, &swap);
}

int os_loadavg_get_stats(double *value) {
    uint32_t av[6];

    *value = 0.0;
    if (!statis_get("avenrun", av, sizeof(av)))
        return 0;

    *value = (double)av[1] / LOAD_SCALE;
    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    unsigned char *buf, *r, *end;
    uint32_t in, out, sum_in = 0, sum_out = 0;
    size_t len = 4 + MAX_RECS * IF_REC;
    int all = IF_IS_ALL(interface_name);
    int found = 0;

    if (!interface_name)
        return 0;

    buf = malloc(len);
    if (!buf)
        return 0;
    if (!statis_get("netstats", buf, len)) {
        free(buf);
        return 0;
    }

    end = buf + len;
    for (r = buf + 4; r + IF_REC <= end && r[0]; r += IF_REC) {
        memcpy(&in, r + 44, sizeof(in));
        memcpy(&out, r + 48, sizeof(out));

        if (all) {
            if (IF_IS_LOOPBACK((char *)r))
                continue;
            sum_in += in;
            sum_out += out;
            found = 1;
            continue;
        }

        if (strncmp((char *)r, interface_name, 16) != 0)
            continue;
        sum_in = in;
        sum_out = out;
        found = 1;
        break;
    }

    free(buf);
    if (!found)
        return 0;

    *in_bytes = sum_in;
    *out_bytes = sum_out;
    return 1;
}

int os_get_default_gw_ip(char *buf, size_t buflen) {
    unsigned char *rt, *r, *end;
    struct sockaddr_in dst, gw;
    unsigned short flags;
    size_t len = 4 + MAX_RECS * RT_REC;
    int found = 0;

    if (!buf || buflen < 16)
        return 0;

    rt = malloc(len);
    if (!rt)
        return 0;
    if (!statis_get("routes", rt, len)) {
        free(rt);
        return 0;
    }

    end = rt + len;
    for (r = rt + 4; r + RT_REC <= end; r += RT_REC) {
        memcpy(&dst, r, sizeof(dst));
        if (dst.sin_family != AF_INET)
            break;
        memcpy(&gw, r + 16, sizeof(gw));
        memcpy(&flags, r + 32, sizeof(flags));
        if (dst.sin_addr.s_addr != INADDR_ANY || !(flags & RTF_GATEWAY))
            continue;
        snprintf(buf, buflen, "%s", inet_ntoa(gw.sin_addr));
        found = 1;
        break;
    }

    free(rt);
    return found;
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

/* libpthread expects the native compiler's thread startup module to supply
 * this C++ unwind hook; C code has nothing to unwind. */
void __thread_unwind(void) {
}

/* libmproc's versions of these two put a branch in a branch delay slot to pick
 * an ll/sc loop by cache line. An R4000 runs that, QEMU traps it as an illegal
 * instruction. Defined here they pre-empt libmproc for libpthread as well. */
int atomic_cas_inc(int *p, int v) {
    int t;

    __asm__ __volatile__(
        "   .set    push\n"
        "   .set    mips2\n"
        "   .set    noreorder\n"
        "1: ll      %0, 0(%1)\n"
        "   addu    %0, %0, %2\n"
        "   sc      %0, 0(%1)\n"
        "   beqz    %0, 1b\n"
        "   nop\n"
        "   .set    pop\n"
        : "=&r" (t) : "r" (p), "r" (v) : "memory");
    return 1;
}

int atomic_cas_ptr(void **p, void *old, void *new) {
    int t, ok;

    __asm__ __volatile__(
        "   .set    push\n"
        "   .set    mips2\n"
        "   .set    noreorder\n"
        "1: ll      %0, 0(%2)\n"
        "   bne     %0, %3, 2f\n"
        "   li      %1, 0\n"
        "   move    %0, %4\n"
        "   sc      %0, 0(%2)\n"
        "   beqz    %0, 1b\n"
        "   li      %1, 1\n"
        "2:\n"
        "   .set    pop\n"
        : "=&r" (t), "=&r" (ok) : "r" (p), "r" (old), "r" (new) : "memory");
    return ok;
}

#include "unix-ping.c"
#include "posix_timer.c"
