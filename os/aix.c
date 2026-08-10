#include "os_interface.h"
#include "rtsock-defgw.c"

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};
#include <nlist.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>

int snprintf(char *, size_t, const char *, ...);

/* 4.1 predates libperfstat, so every statistic is read out of /dev/kmem at an
 * address resolved with knlist(). */
static int kmem_fd = -1;

static unsigned long kmem_symbol(const char *name) {
    struct nlist nl[2];

    memset(nl, 0, sizeof(nl));
    nl[0].n_name = (char *)name;
    nl[1].n_name = NULL;

    if (knlist(nl, 1, sizeof(struct nlist)) != 0)
        return 0;

    return nl[0].n_value;
}

static int kmem_read(unsigned long addr, void *buf, int size) {
    int upper_2gb = 0;

    if (addr == 0)
        return 0;

    if (kmem_fd < 0) {
        kmem_fd = open("/dev/kmem", O_RDONLY);
        if (kmem_fd < 0)
            return 0;
    }

    /* addresses above 2GB are reached by seeking to addr % 2GB and passing 1
     * as the extension argument of readx(), see the kmem(4) man page */
    if (addr > 0x7fffffff) {
        upper_2gb = 1;
        addr &= 0x7fffffff;
    }

    if (lseek(kmem_fd, addr, SEEK_SET) == -1)
        return 0;

    return readx(kmem_fd, buf, size, upper_2gb) == size;
}

/* The kernel publishes its virtual memory counters through the "vmker"
 * symbol, for which AIX ships no header.  This layout was reverse engineered
 * by Jussi Maki for "monitor" and is the same one the AIX 4.1 module of
 * top(1) uses; numperm is named after vmtune.c.  Verified on 4.1.5: totalmem
 * matches "lsattr -El sys0 -a realmem" and freemem tracks vmstat's fre. */
struct vmker {
    unsigned int n0, n1, n2, n3, n4, n5, n6, n7, n8;
    unsigned int totalmem;              /* real memory frames */
    unsigned int badmem;                /* unusable frames */
    unsigned int freemem;               /* free real memory frames */
    unsigned int n12;
    unsigned int numperm;               /* persistent (file cache) pages */
    unsigned int totalvmem, freevmem;   /* paging space frames */
    unsigned int n16, n17, n18, n19, n20;
};

const char* os_get_platform_name(void) {
    return "aix";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats(double *value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static unsigned long sysinfo_addr = 0;
    static int first_time = 1;
    struct sysinfo s_info;
    uint64_t user, sys, wait, idle;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;

    if (first_time) {
        sysinfo_addr = kmem_symbol("sysinfo");
        first_time = 0;
    }

    if (!kmem_read(sysinfo_addr, &s_info, sizeof(s_info))) {
        return 0;
    }

    user = s_info.cpu[CPU_USER];
    sys = s_info.cpu[CPU_KERNEL];
    wait = s_info.cpu[CPU_WAIT];
    idle = s_info.cpu[CPU_IDLE];
    total_ticks = user + sys + wait + idle;

    if (prev_total != 0 && total_ticks > prev_total) {
        idle_diff = idle - prev_idle;
        total_diff = total_ticks - prev_total;
        *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
    } else {
        *value = 0.0;
    }

    prev_idle = idle;
    prev_total = total_ticks;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;
    static unsigned long sysinfo_addr = 0;
    static int first_time = 1;
    struct sysinfo s_info;
    uint64_t user, sys, wait, idle;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;

    if (first_time) {
        sysinfo_addr = kmem_symbol("sysinfo");
        first_time = 0;
    }

    if (!kmem_read(sysinfo_addr, &s_info, sizeof(s_info))) {
        return 0;
    }

    user = s_info.cpu[CPU_USER];
    sys = s_info.cpu[CPU_KERNEL];
    wait = s_info.cpu[CPU_WAIT];
    idle = s_info.cpu[CPU_IDLE];
    total_ticks = user + sys + wait + idle;
    system_ticks = sys;

    if (prev_total != 0 && total_ticks > prev_total) {
        idle_diff = idle - prev_idle;
        total_diff = total_ticks - prev_total;
        system_diff = system_ticks - prev_system;

        *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
        *system_value = 100.0 * (double)system_diff / total_diff;
    } else {
        *total_value = 0.0;
        *system_value = 0.0;
    }

    prev_idle = idle;
    prev_total = total_ticks;
    prev_system = system_ticks;

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    return 1;
}

int os_memory_get_stats(double *value) {
    static unsigned long vmker_addr = 0;
    static long pagesize = 0;
    static int first_time = 1;
    struct vmker vmk;
    uint64_t total_memory, free_memory, used_memory;

    if (first_time) {
        vmker_addr = kmem_symbol("vmker");
        pagesize = getpagesize();
        first_time = 0;
    }

    if (!kmem_read(vmker_addr, &vmk, sizeof(vmk)))
        return 0;

    total_memory = (uint64_t)vmk.totalmem * pagesize;
    if (total_memory == 0) return 0;

    /* numperm is the file cache, which the kernel hands back under pressure,
     * so count it as available rather than used */
    free_memory = ((uint64_t)vmk.freemem + (uint64_t)vmk.numperm) * pagesize;
    used_memory = total_memory > free_memory ? total_memory - free_memory : 0;
    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_loadavg_get_stats(double *value) {
    static unsigned long load_avg_addr = 0;
    static int first_time = 1;
    int load_avg[3];

    if (first_time) {
        load_avg_addr = kmem_symbol("avenrun");
        first_time = 0;
    }

    *value = 0.0;
    if (kmem_read(load_avg_addr, load_avg, sizeof(load_avg)))
        *value = (double)load_avg[0] / 65536.0;

    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    static unsigned long ifnet_addr = 0;
    static int first_time = 1;
    unsigned long ifnetaddr;
    struct ifnet ifnet_buf;
    char name_buf[16];
    char ifname_full[32];

    if (!interface_name || !in_bytes || !out_bytes) {
        return 0;
    }

    if (first_time) {
        ifnet_addr = kmem_symbol("ifnet");
        first_time = 0;
    }

    if (!kmem_read(ifnet_addr, &ifnetaddr, sizeof(ifnetaddr))) {
        return 0;
    }

    while (ifnetaddr) {
        if (!kmem_read(ifnetaddr, &ifnet_buf, sizeof(ifnet_buf))) {
            break;
        }

        if (!kmem_read((unsigned long)ifnet_buf.if_name, name_buf, sizeof(name_buf))) {
            break;
        }

        name_buf[sizeof(name_buf)-1] = '\0';
        snprintf(ifname_full, sizeof(ifname_full), "%s%d", name_buf, ifnet_buf.if_unit);

        if (strcmp(ifname_full, interface_name) == 0) {
            *in_bytes = (uint32_t)(ifnet_buf.if_ibytes & 0xffffffff);
            *out_bytes = (uint32_t)(ifnet_buf.if_obytes & 0xffffffff);
            return 1;
        }

        ifnetaddr = (unsigned long)ifnet_buf.if_next;
    }

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

    {
        pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
        memcpy(mutex->handle, &init_mutex, sizeof(pthread_mutex_t));
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

char *os_get_config_path(const char *filename) {
    return (char *)filename;
}

#include "unix-ping.c"
#include "posix_timer.c"
