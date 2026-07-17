#define _KMEMUSER

#include "os_interface.h"
#ifndef IRIX5
#include "unix-defgw.c"
#endif

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/sysmp.h>
#include <nlist.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#ifdef IRIX5
#include <sys/prctl.h>
#include <sys/wait.h>
#include <abi_mutex.h>
#else
#include <pthread.h>
#endif
#include <stdlib.h>
#include <sys/socket.h>
#ifndef IRIX5
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#endif

#ifdef IRIX5
/*
 * 5.3 libc predates snprintf and usleep. Supply real symbols (not compat.h
 * macros) over vsprintf/nanosleep: gfx/x11.c and others call these without
 * including compat.h, so only an actual definition resolves every reference.
 * Neither has a 5.3 prototype, so there is nothing to collide with.
 *
 * Use gcc's varargs builtins, not <stdarg.h>: -isystem /usr/include shadows
 * gcc's copy with IRIX's, which uses __builtin_alignof that gcc 4.5 lacks.
 */
int snprintf(char *buf, size_t size, const char *fmt, ...) {
    __builtin_va_list ap;
    int n;
    (void)size;
    __builtin_va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int usleep(unsigned int usec) {
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    nanosleep(&ts, NULL);
    return 0;
}
#endif

#define UNIX "/unix"
#define KMEM "/dev/kmem"
#define CPUSTATES 6

#ifndef FSCALE
#define FSHIFT 8
#define FSCALE (1<<FSHIFT)
#endif

#define X_AVENRUN 0
#define X_FREEMEM 1
#define X_MAXMEM 2
#define X_AVAILRMEM 3

static struct nlist nlst[] = {
    { "avenrun" },
    { "freemem" },
    { "maxmem" },
    { "availrmem" },
    { 0 }
};

static int kmem = -1;
static int nlist_initialized = 0;
static int pagesize = 0;

static int getkval(off_t offset, int *ptr, int size) {
    if (lseek(kmem, offset, SEEK_SET) == -1)
        return 0;
    if (read(kmem, (char *)ptr, size) == -1)
        return 0;
    return 1;
}

const char* os_get_platform_name(void) {
    return "irix65";
}

int os_init(void) {
    kmem = open(KMEM, O_RDONLY);
    if (kmem == -1)
        return 0;

    if (nlist(UNIX, nlst) == -1) {
        close(kmem);
        kmem = -1;
        return 0;
    }

    if (nlst[0].n_type == 0) {
        close(kmem);
        kmem = -1;
        return 0;
    }

    nlist_initialized = 1;
    pagesize = getpagesize();
    return 1;
}

void os_cleanup(void) {
    if (kmem != -1) {
        close(kmem);
        kmem = -1;
    }
}

int os_cpu_get_stats(double *value) {
    static long cp_old[CPUSTATES];
    static int first_time = 1;
    struct sysinfo sysinfo;
    long cp_new[CPUSTATES];
    long cp_diff[CPUSTATES];
    long total;
    int i;

    if (sysmp(MP_SAGET, MPSA_SINFO, &sysinfo, sizeof(struct sysinfo)) == -1)
        return 0;

    for (i = 0; i < CPUSTATES; i++)
        cp_new[i] = sysinfo.cpu[i];

    if (first_time) {
        for (i = 0; i < CPUSTATES; i++)
            cp_old[i] = cp_new[i];
        first_time = 0;
        *value = 0.0;
        return 1;
    }

    total = 0;
    for (i = 0; i < CPUSTATES; i++) {
        cp_diff[i] = cp_new[i] - cp_old[i];
        total += cp_diff[i];
        cp_old[i] = cp_new[i];
    }

    if (total == 0)
        total = 1;

    *value = 100.0 * (1.0 - (double)cp_diff[CPU_IDLE] / total);

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static long cp_old[CPUSTATES];
    static int first_time = 1;
    struct sysinfo sysinfo;
    long cp_new[CPUSTATES];
    long cp_diff[CPUSTATES];
    long total;
    int i;

    if (sysmp(MP_SAGET, MPSA_SINFO, &sysinfo, sizeof(struct sysinfo)) == -1)
        return 0;

    for (i = 0; i < CPUSTATES; i++)
        cp_new[i] = sysinfo.cpu[i];

    if (first_time) {
        for (i = 0; i < CPUSTATES; i++)
            cp_old[i] = cp_new[i];
        first_time = 0;
        *total_value = 0.0;
        *system_value = 0.0;
        return 1;
    }

    total = 0;
    for (i = 0; i < CPUSTATES; i++) {
        cp_diff[i] = cp_new[i] - cp_old[i];
        total += cp_diff[i];
        cp_old[i] = cp_new[i];
    }

    if (total == 0)
        total = 1;

    *total_value = 100.0 * (1.0 - (double)cp_diff[CPU_IDLE] / total);
    *system_value = 100.0 * ((double)cp_diff[CPU_KERNEL] + (double)cp_diff[CPU_INTR]) / total;

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    return 1;
}

int os_memory_get_stats(double *value) {
    int freemem, maxmem, availrmem;
    uint64_t total_memory, free_memory, used_memory;

    if (!nlist_initialized || kmem == -1)
        return 0;

    if (!getkval(nlst[X_FREEMEM].n_value, &freemem, sizeof(freemem)))
        return 0;

    if (!getkval(nlst[X_MAXMEM].n_value, &maxmem, sizeof(maxmem)))
        return 0;

    if (!getkval(nlst[X_AVAILRMEM].n_value, &availrmem, sizeof(availrmem)))
        availrmem = freemem;

    total_memory = (uint64_t)maxmem * pagesize;
    free_memory = (uint64_t)freemem * pagesize;

    if (total_memory == 0)
        return 0;

    used_memory = total_memory - free_memory;
    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_loadavg_get_stats(double *value) {
    int avenrun[3];

    if (!nlist_initialized || kmem == -1)
        return 0;

    if (!getkval(nlst[X_AVENRUN].n_value, avenrun, sizeof(avenrun)))
        return 0;

    *value = (double)avenrun[0] / FSCALE / 1024.0;

    if (*value < 0.0)
        *value = 0.0;

    return 1;
}

#ifdef IRIX5

/* 5.3 predates the sysctl(3) route dump. netstat is the only view of the
 * routing table that does not mean walking the radix tree through /dev/kmem;
 * the hpux 10.20 branch takes the same way out. */
int os_get_default_gw_ip(char *buf, size_t buflen) {
    char line[256], dest[64], gw[64];
    FILE *fp;
    int found;

    if (!buf || buflen < 16) return 0;

    fp = popen("/usr/etc/netstat -rn 2>/dev/null", "r");
    if (!fp) return 0;

    found = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63s %63s", dest, gw) != 2) continue;
        if (strcmp(dest, "default") != 0 && strcmp(dest, "0.0.0.0") != 0) continue;
        snprintf(buf, buflen, "%s", gw);
        found = 1;
        break;
    }
    pclose(fp);

    return found;
}

/* NET_RT_IFLIST has no 5.3 equivalent: the byte counters live in the kernel
 * ifnet chain, reachable only through /dev/kmem. Until that is written,
 * bw=local reports nothing here; bw=snmp1 against the host still works. */
int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    (void)interface_name;
    (void)in_bytes;
    (void)out_bytes;
    return 0;
}

#else

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    static char *buf = NULL;
    static size_t buf_size = 0;
    int mib[6];
    size_t len;
    char *next, *lim;
    struct if_msghdr *ifm;
    struct sockaddr_dl *sdl;
    char ifname[32];

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = 0;
    mib[4] = NET_RT_IFLIST;
    mib[5] = 0;

    if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0)
        return 0;

    if (len > buf_size) {
        char *new_buf;
        new_buf = realloc(buf, len);
        if (!new_buf)
            return 0;
        buf = new_buf;
        buf_size = len;
    }

    len = buf_size;
    if (sysctl(mib, 6, buf, &len, NULL, 0) < 0)
        return 0;

    lim = buf + len;
    for (next = buf; next < lim;) {
        ifm = (struct if_msghdr *)next;

        if (ifm->ifm_type == RTM_IFINFO) {
            sdl = (struct sockaddr_dl *)(ifm + 1);
            if (sdl->sdl_family == AF_LINK) {
                memcpy(ifname, sdl->sdl_data, sdl->sdl_nlen);
                ifname[sdl->sdl_nlen] = '\0';

                if (strcmp(ifname, interface_name) == 0) {
                    *in_bytes = (uint32_t)ifm->ifm_data.ifi_ibytes;
                    *out_bytes = (uint32_t)ifm->ifm_data.ifi_obytes;
                    return 1;
                }
            }
        }

        next += ifm->ifm_msglen;
    }

    return 0;
}

#endif /* IRIX5 */

void os_sleep(uint32_t milliseconds) {
    struct timespec ts;

    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

uint32_t os_get_time_ms(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

#ifdef IRIX5

/*
 * 5.3 has no pthreads. sproc(PR_SADDR) forks a process that shares the whole
 * address space, so globals, heap, and the abilock below are common to every
 * collector; PR_SFDS shares the fd table too. abi_mutex is a bare test-and-set
 * spinlock, which is all ringbuf's sub-microsecond critical sections need.
 */

plot_mutex_t *os_plot_mutex_create(void) {
    plot_mutex_t *mutex;

    mutex = malloc(sizeof(plot_mutex_t));
    if (!mutex) return NULL;

    mutex->handle = malloc(sizeof(abilock_t));
    if (!mutex->handle) {
        free(mutex);
        return NULL;
    }

    init_lock((abilock_t*)mutex->handle);
    return mutex;
}

void os_plot_mutex_destroy(plot_mutex_t *mutex) {
    if (!mutex) return;

    free(mutex->handle);
    free(mutex);
}

void os_plot_mutex_lock(plot_mutex_t *mutex) {
    if (!mutex) return;

    spin_lock((abilock_t*)mutex->handle);
}

void os_plot_mutex_unlock(plot_mutex_t *mutex) {
    if (!mutex) return;

    release_lock((abilock_t*)mutex->handle);
}

plot_thread_t *os_plot_thread_create(void (*func)(void *), void *arg) {
    plot_thread_t *thread;
    int pid;

    thread = malloc(sizeof(plot_thread_t));
    if (!thread) return NULL;

    thread->handle = malloc(sizeof(int));
    if (!thread->handle) {
        free(thread);
        return NULL;
    }

    pid = sproc(func, PR_SADDR | PR_SFDS, arg);
    if (pid < 0) {
        free(thread->handle);
        free(thread);
        return NULL;
    }

    *(int*)thread->handle = pid;
    return thread;
}

void os_plot_thread_destroy(plot_thread_t *thread) {
    if (!thread) return;

    free(thread->handle);
    free(thread);
}

void os_plot_thread_join(plot_thread_t *thread) {
    if (!thread) return;

    waitpid(*(int*)thread->handle, NULL, 0);
}

int os_plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms) {
    (void)timeout_ms;

    if (!thread) return 0;

    return waitpid(*(int*)thread->handle, NULL, 0) >= 0;
}

#else

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

    result = pthread_join(*(pthread_t*)thread->handle, NULL);
    return (result == 0);
}

#endif /* IRIX5 */

char *os_get_config_path(const char *filename) {
    return (char *)filename;
}

#include "unix-ping.c"
#ifdef IRIX5
#include "sproc_timer.c"
#else
#include "posix_timer.c"
#endif
