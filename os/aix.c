#include "os_interface.h"

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
    static int kmem_fd = -1;
    static unsigned long sysinfo_addr = 0;
    static int first_time = 1;
    struct sysinfo s_info;
    uint64_t user, sys, wait, idle;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;
    unsigned long offset;
    int upper_2gb;

    if (first_time) {
        struct nlist nl[2];
        nl[0].n_name = "sysinfo";
        nl[0].n_value = 0;
        nl[1].n_name = NULL;
        nl[1].n_value = 0;

        if (knlist(nl, 1, sizeof(struct nlist)) == 0 && nl[0].n_value != 0) {
            sysinfo_addr = nl[0].n_value;
            kmem_fd = open("/dev/kmem", O_RDONLY);
        }
        first_time = 0;
    }

    if (kmem_fd < 0 || sysinfo_addr == 0) {
        return 0;
    }

    offset = sysinfo_addr;
    upper_2gb = 0;
    if (offset > (1U << 31)) {
        upper_2gb = 1;
        offset &= 0x7fffffff;
    }

    if (lseek(kmem_fd, offset, SEEK_SET) == -1) {
        return 0;
    }

    if (readx(kmem_fd, &s_info, sizeof(s_info), upper_2gb) != sizeof(s_info)) {
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
    static int kmem_fd = -1;
    static unsigned long sysinfo_addr = 0;
    static int first_time = 1;
    struct sysinfo s_info;
    uint64_t user, sys, wait, idle;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;
    unsigned long offset;
    int upper_2gb;

    if (first_time) {
        struct nlist nl[2];
        nl[0].n_name = "sysinfo";
        nl[0].n_value = 0;
        nl[1].n_name = NULL;
        nl[1].n_value = 0;

        if (knlist(nl, 1, sizeof(struct nlist)) == 0 && nl[0].n_value != 0) {
            sysinfo_addr = nl[0].n_value;
            kmem_fd = open("/dev/kmem", O_RDONLY);
        }
        first_time = 0;
    }

    if (kmem_fd < 0 || sysinfo_addr == 0) {
        return 0;
    }

    offset = sysinfo_addr;
    upper_2gb = 0;
    if (offset > (1U << 31)) {
        upper_2gb = 1;
        offset &= 0x7fffffff;
    }

    if (lseek(kmem_fd, offset, SEEK_SET) == -1) {
        return 0;
    }

    if (readx(kmem_fd, &s_info, sizeof(s_info), upper_2gb) != sizeof(s_info)) {
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
    return 0;
}

int os_loadavg_get_stats(double *value) {
    static int kmem_fd = -1;
    static unsigned long load_avg_addr = 0;
    static int first_time = 1;
    int load_avg[3];
    unsigned long offset;
    int upper_2gb;

    if (first_time) {
        struct nlist nl[2];
        nl[0].n_name = "avenrun";
        nl[0].n_value = 0;
        nl[1].n_name = NULL;
        nl[1].n_value = 0;

        if (knlist(nl, 1, sizeof(struct nlist)) == 0 && nl[0].n_value != 0) {
            load_avg_addr = nl[0].n_value;
            kmem_fd = open("/dev/kmem", O_RDONLY);
        }
        first_time = 0;
    }

    if (kmem_fd >= 0 && load_avg_addr != 0) {
        offset = load_avg_addr;
        upper_2gb = 0;
        if (offset > (1U << 31)) {
            upper_2gb = 1;
            offset &= 0x7fffffff;
        }

        if (lseek(kmem_fd, offset, SEEK_SET) != -1 &&
            readx(kmem_fd, load_avg, sizeof(load_avg), upper_2gb) == sizeof(load_avg)) {
            *value = (double)load_avg[0] / 65536.0;
        } else {
            *value = 0.0;
        }
    } else {
        *value = 0.0;
    }
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    static int kmem_fd = -1;
    static int initialized = 0;
    struct nlist nl[2];
    unsigned long ifnetaddr;
    struct ifnet ifnet_buf;
    char name_buf[16];
    char ifname_full[32];

    if (!interface_name || !in_bytes || !out_bytes) {
        return 0;
    }

    if (!initialized) {
        kmem_fd = open("/dev/kmem", O_RDONLY);
        initialized = 1;
    }

    if (kmem_fd < 0) {
        return 0;
    }

    memset(nl, 0, sizeof(nl));
    nl[0].n_name = "ifnet";
    nl[1].n_name = NULL;

    if (nlist("/unix", nl) < 0) {
        return 0;
    }

    if (nl[0].n_value == 0) {
        return 0;
    }

    if (lseek(kmem_fd, nl[0].n_value, SEEK_SET) < 0) {
        return 0;
    }

    if (read(kmem_fd, &ifnetaddr, sizeof(ifnetaddr)) != sizeof(ifnetaddr)) {
        return 0;
    }

    while (ifnetaddr) {
        if (lseek(kmem_fd, ifnetaddr, SEEK_SET) < 0) {
            break;
        }

        if (read(kmem_fd, &ifnet_buf, sizeof(ifnet_buf)) != sizeof(ifnet_buf)) {
            break;
        }

        if (lseek(kmem_fd, (unsigned long)ifnet_buf.if_name, SEEK_SET) < 0) {
            break;
        }

        if (read(kmem_fd, name_buf, sizeof(name_buf)) != sizeof(name_buf)) {
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
    char *home;
    static char config_path[512];
    char *xdg_config;

    home = getenv("HOME");
    if (!home) return NULL;

    xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
        snprintf(config_path, sizeof(config_path), "%s/sng/%s", xdg_config, filename);
    } else {
        snprintf(config_path, sizeof(config_path), "%s/.config/sng/%s", home, filename);
    }

    return config_path;
}

#include "unix-ping.c"

struct os_ping_context_t {
    sryze_ping_context_t *sryze_ctx;
};

os_ping_context_t *os_ping_create(const char *hostname, uint32_t timeout_ms) {
    os_ping_context_t *ctx;

    ctx = malloc(sizeof(os_ping_context_t));
    if (!ctx) return NULL;

    ctx->sryze_ctx = sryze_ping_create(hostname, timeout_ms);
    if (!ctx->sryze_ctx) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

int os_ping_send(os_ping_context_t *ctx, double *ping_time_ms) {
    if (!ctx) return 0;
    return sryze_ping_send(ctx->sryze_ctx, ping_time_ms);
}

void os_ping_destroy(os_ping_context_t *ctx) {
    if (!ctx) return;
    if (ctx->sryze_ctx) {
        sryze_ping_destroy(ctx->sryze_ctx);
    }
    free(ctx);
}
