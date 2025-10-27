#include "os_interface.h"

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};
#include <sys/pstat.h>
#include <sys/mib.h>
#include <netinet/mib_kern.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>

const char* os_get_platform_name(void) {
    return "hpux";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats(double *value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    struct pst_dynamic stats;

    if (pstat_getdynamic(&stats, sizeof(struct pst_dynamic), 1, 0) == -1)
        return 0;

    uint64_t user, nice, sys, idle, intr;
    uint64_t total_ticks;

    user = stats.psd_cpu_time[0];
    nice = stats.psd_cpu_time[1];
    sys = stats.psd_cpu_time[2];
    idle = stats.psd_cpu_time[3];
    intr = stats.psd_cpu_time[4];
    total_ticks = user + nice + sys + idle + intr;

    if (prev_total != 0 && total_ticks > prev_total) {
        uint64_t idle_diff = idle - prev_idle;
        uint64_t total_diff = total_ticks - prev_total;
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
    struct pst_dynamic stats;

    if (pstat_getdynamic(&stats, sizeof(struct pst_dynamic), 1, 0) == -1)
        return 0;

    uint64_t user, nice, sys, idle, intr;
    uint64_t total_ticks;

    user = stats.psd_cpu_time[0];
    nice = stats.psd_cpu_time[1];
    sys = stats.psd_cpu_time[2];
    idle = stats.psd_cpu_time[3];
    intr = stats.psd_cpu_time[4];
    total_ticks = user + nice + sys + idle + intr;
    uint64_t system_ticks = sys + intr;

    if (prev_total != 0 && total_ticks > prev_total) {
        uint64_t idle_diff = idle - prev_idle;
        uint64_t total_diff = total_ticks - prev_total;
        uint64_t system_diff = system_ticks - prev_system;

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
    struct pst_static pststatic;
    struct pst_dynamic pstdynamic;
    uint64_t total_memory, free_memory, used_memory;

    if (pstat_getstatic(&pststatic, sizeof(struct pst_static), 1, 0) == -1)
        return 0;

    if (pstat_getdynamic(&pstdynamic, sizeof(struct pst_dynamic), 1, 0) == -1)
        return 0;

    total_memory = pststatic.physical_memory * pststatic.page_size;
    free_memory = pstdynamic.psd_free * pststatic.page_size;

    if (total_memory == 0) return 0;

    used_memory = total_memory - free_memory;
    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_loadavg_get_stats(double *value) {
    struct pst_dynamic pstd;

    if (pstat_getdynamic(&pstd, sizeof(pstd), 1, 0) != -1) {
        *value = pstd.psd_avg_1_min;
    } else {
        *value = 0.0;
    }
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    static nmapi_phystat *if_list = NULL;
    static int if_count = 0;
    int fd;
    struct nmparms p;
    int val;
    unsigned int ulen;
    int i;

    if (if_count == 0) {
        fd = open_mib("/dev/ip", O_RDONLY, 0, NM_ASYNC_OFF);
        if (fd < 0) {
            return 0;
        }

        p.objid = ID_ifNumber;
        p.buffer = (void *) &val;
        ulen = sizeof(int);
        p.len = &ulen;

        if (get_mib_info(fd, &p) != 0) {
            close_mib(fd);
            return 0;
        }

        if_count = val;
        close_mib(fd);

        if (if_count <= 0) {
            return 0;
        }

        if_list = (nmapi_phystat *) malloc(sizeof(nmapi_phystat) * if_count);
        if (if_list == NULL) {
            return 0;
        }
    }

    ulen = (unsigned int) if_count * sizeof(nmapi_phystat);
    if (get_physical_stat(if_list, &ulen) < 0) {
        return 0;
    }

    for (i = 0; i < if_count; i++) {
        if (strcmp(if_list[i].nm_device, interface_name) == 0) {
            *in_bytes = (uint32_t)if_list[i].if_entry.ifInOctets;
            *out_bytes = (uint32_t)if_list[i].if_entry.ifOutOctets;
            return 1;
        }
    }

    return 0;
}

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
