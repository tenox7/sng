/* OpenVMS platform layer. Threads via POSIX Threads (PTHREAD$RTL),
 * time via gettimeofday, sleeps via pthread_delay_np (present on all
 * DECthreads including VAX 7.3, unlike usleep/nanosleep). The timer
 * uses timespec arithmetic only - VAX has no 64-bit integers.
 * Ping via unix-ping.c raw ICMP sockets (needs SYSPRV).
 * CPU: wildcard $GETJPI scan summing per-process CPUTIM deltas.
 * Memory (VAX): free/modified list cells read directly - the image
 * must link against SYS$SYSTEM:SYS.STB (see sng.opt).
 * Loadavg/interface stats not implemented yet. */
#include "os_interface.h"

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <ssdef.h>
#include <syidef.h>
#include <jpidef.h>
#include <starlet.h>

typedef struct {
    unsigned short buflen;
    unsigned short itmcod;
    void *bufadr;
    unsigned short *retlen;
} vms_item_t;

typedef struct {
    unsigned short sts;
    unsigned short count;
    unsigned int extra;
} vms_iosb_t;

#ifdef __VAX
/* scheduler free/modified page list cells, resolved from SYS.STB */
extern unsigned int sch$gl_freecnt;
extern unsigned int sch$gl_mfycnt;
#endif

static unsigned int vms_getsyi_long(unsigned short itmcod, unsigned int dflt) {
    unsigned int val = 0;
    unsigned short retlen = 0;
    vms_item_t items[2];
    vms_iosb_t iosb;
    unsigned int status;

    items[0].buflen = sizeof(val);
    items[0].itmcod = itmcod;
    items[0].bufadr = &val;
    items[0].retlen = &retlen;
    items[1].buflen = 0;
    items[1].itmcod = 0;
    items[1].bufadr = 0;
    items[1].retlen = 0;

    status = sys$getsyiw(0, 0, 0, items, &iosb, 0, 0);
    if (!(status & 1) || !(iosb.sts & 1)) return dflt;
    return val;
}

#if defined(__CRTL_VER) && (__CRTL_VER < 70312000)
int sng_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int n;
    (void)size;
    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}
#endif

const char* os_get_platform_name(void) {
    return "openvms";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static unsigned int prev_ticks = 0;
    static uint32_t prev_ms = 0;
    static unsigned int ncpu = 0;
    static int first_time = 1;
    unsigned int pidctx;
    unsigned int cputim;
    unsigned int sum;
    unsigned int status;
    unsigned int guard;
    unsigned int dticks;
    uint32_t now_ms, dwall;
    unsigned short retlen;
    vms_item_t items[2];
    vms_iosb_t iosb;

    if (!total_value || !system_value) return 0;

    if (ncpu == 0) {
        ncpu = vms_getsyi_long(SYI$_ACTIVECPU_CNT, 1);
        if (ncpu == 0) ncpu = 1;
    }

    items[0].buflen = sizeof(cputim);
    items[0].itmcod = JPI$_CPUTIM;
    items[0].bufadr = &cputim;
    items[0].retlen = &retlen;
    items[1].buflen = 0;
    items[1].itmcod = 0;
    items[1].bufadr = 0;
    items[1].retlen = 0;

    /* CPUTIM is in 10ms ticks; sum over all processes via wildcard scan */
    sum = 0;
    pidctx = 0xFFFFFFFF;
    for (guard = 0; guard < 16384; guard++) {
        cputim = 0;
        status = sys$getjpiw(0, &pidctx, 0, items, &iosb, 0, 0);
        if (status == SS$_NOMOREPROC) break;
        if ((status & 1) && (iosb.sts & 1)) sum += cputim;
    }

    now_ms = os_get_time_ms();

    if (first_time) {
        first_time = 0;
        prev_ticks = sum;
        prev_ms = now_ms;
        *total_value = 0.0;
        *system_value = 0.0;
        return 1;
    }

    dwall = now_ms - prev_ms;
    /* sum can drop when processes exit and take their time with them */
    dticks = (sum >= prev_ticks) ? sum - prev_ticks : 0;
    prev_ticks = sum;
    prev_ms = now_ms;

    if (dwall == 0) {
        *total_value = 0.0;
        *system_value = 0.0;
        return 1;
    }

    *total_value = 100.0 * ((double)dticks * 10.0) / ((double)dwall * ncpu);
    *system_value = 0.0; /* no per-mode split available via $GETJPI */

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;

    return 1;
}

int os_cpu_get_stats(double *value) {
    double total, system;

    if (!value) return 0;
    if (!os_cpu_get_stats_dual(&total, &system)) return 0;
    *value = total;
    return 1;
}

int os_memory_get_stats(double *value) {
#ifdef __VAX
    static unsigned int memsize = 0;
    unsigned int avail;

    if (!value) return 0;

    if (memsize == 0) {
        memsize = vms_getsyi_long(SYI$_MEMSIZE, 0);
        if (memsize == 0) return 0;
    }

    avail = sch$gl_freecnt + sch$gl_mfycnt;
    if (avail > memsize) avail = memsize;

    *value = 100.0 * (double)(memsize - avail) / (double)memsize;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
#else
    /* Alpha/I64: use SYI$_FREE_PAGE_COUNT when that port happens */
    (void)value;
    return 0;
#endif
}

int os_loadavg_get_stats(double *value) {
    (void)value;
    return 0;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    (void)interface_name;
    (void)in_bytes;
    (void)out_bytes;
    return 0;
}

void os_sleep(uint32_t milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    pthread_delay_np(&ts);
}

uint32_t os_get_time_ms(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec * 1000 + (uint32_t)tv.tv_usec / 1000;
}

plot_mutex_t *os_plot_mutex_create(void) {
    plot_mutex_t *mutex;
    int ret;

    mutex = malloc(sizeof(plot_mutex_t));
    if (!mutex) return NULL;

    mutex->handle = malloc(sizeof(pthread_mutex_t));
    if (!mutex->handle) {
        free(mutex);
        return NULL;
    }

    ret = pthread_mutex_init((pthread_mutex_t*)mutex->handle, NULL);
    if (ret != 0) {
        free(mutex->handle);
        free(mutex);
        return NULL;
    }

    return mutex;
}

void os_plot_mutex_destroy(plot_mutex_t *mutex) {
    if (!mutex) return;

    pthread_mutex_destroy((pthread_mutex_t*)mutex->handle);
    free(mutex->handle);
    free(mutex);
}

void os_plot_mutex_lock(plot_mutex_t *mutex) {
    if (!mutex) return;

    pthread_mutex_lock((pthread_mutex_t*)mutex->handle);
}

void os_plot_mutex_unlock(plot_mutex_t *mutex) {
    if (!mutex) return;

    pthread_mutex_unlock((pthread_mutex_t*)mutex->handle);
}

plot_thread_t *os_plot_thread_create(void (*func)(void *), void *arg) {
    plot_thread_t *thread;
    int ret;

    thread = malloc(sizeof(plot_thread_t));
    if (!thread) return NULL;

    thread->handle = malloc(sizeof(pthread_t));
    if (!thread->handle) {
        free(thread);
        return NULL;
    }

    ret = pthread_create((pthread_t*)thread->handle, NULL, (void*(*)(void*))func, arg);
    if (ret != 0) {
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

    pthread_join(*(pthread_t*)thread->handle, NULL);
}

int os_plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms) {
    int result;

    if (!thread) return 0;

    (void)timeout_ms;
    result = pthread_join(*(pthread_t*)thread->handle, NULL);
    return (result == 0);
}

char *os_get_config_path(const char *filename) {
    static char config_path[512];

    if (!filename) return NULL;
    if (strchr(filename, ':') || strchr(filename, '[') || strchr(filename, '/'))
        return (char *)filename;

    snprintf(config_path, sizeof(config_path), "SYS$LOGIN:%s", filename);
    return config_path;
}

int os_get_default_gw_ip(char *buf, size_t buflen) {
    (void)buf;
    (void)buflen;
    return 0;
}

/* Periodic timer: absolute-deadline timespec arithmetic, drift-corrected */
struct plot_timer_t {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint32_t interval_ms;
    struct timespec next_deadline;
};

static void plot_timer_ts_add_ms(struct timespec *ts, uint32_t ms) {
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000;
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
}

plot_timer_t *os_plot_timer_create(uint32_t interval_ms) {
    plot_timer_t *timer;
    struct timeval now;

    if (interval_ms == 0) return NULL;

    timer = malloc(sizeof(plot_timer_t));
    if (!timer) return NULL;

    if (pthread_mutex_init(&timer->mutex, NULL) != 0) {
        free(timer);
        return NULL;
    }

    if (pthread_cond_init(&timer->cond, NULL) != 0) {
        pthread_mutex_destroy(&timer->mutex);
        free(timer);
        return NULL;
    }

    timer->interval_ms = interval_ms;
    gettimeofday(&now, NULL);
    timer->next_deadline.tv_sec = now.tv_sec;
    timer->next_deadline.tv_nsec = (long)now.tv_usec * 1000;
    plot_timer_ts_add_ms(&timer->next_deadline, interval_ms);
    return timer;
}

void os_plot_timer_destroy(plot_timer_t *timer) {
    if (!timer) return;
    pthread_cond_destroy(&timer->cond);
    pthread_mutex_destroy(&timer->mutex);
    free(timer);
}

void os_plot_timer_wait(plot_timer_t *timer) {
    int rc;
    struct timeval now;
    unsigned long behind_sec;

    if (!timer) return;

    pthread_mutex_lock(&timer->mutex);

    do {
        rc = pthread_cond_timedwait(&timer->cond, &timer->mutex, &timer->next_deadline);
    } while (rc == 0 || rc == EINTR);

    plot_timer_ts_add_ms(&timer->next_deadline, timer->interval_ms);

    /* fell far behind (suspend, clock jump): restart from now */
    behind_sec = 4UL * timer->interval_ms / 1000;
    if (behind_sec < 30) behind_sec = 30;
    gettimeofday(&now, NULL);
    if ((unsigned long)now.tv_sec > (unsigned long)timer->next_deadline.tv_sec + behind_sec) {
        timer->next_deadline.tv_sec = now.tv_sec;
        timer->next_deadline.tv_nsec = (long)now.tv_usec * 1000;
        plot_timer_ts_add_ms(&timer->next_deadline, timer->interval_ms);
    }

    pthread_mutex_unlock(&timer->mutex);
}

#include "unix-ping.c"
