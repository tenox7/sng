#include "os_interface.h"

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};
#include <kstat.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <thread.h>
#include <stdlib.h>
#include <stdio.h>

const char* os_get_platform_name(void) {
    return "sunos";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats(double *value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static kstat_ctl_t *kc = NULL;
    kstat_t *ksp;
    cpu_stat_t cs;
    uint64_t user, sys, wait, idle;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;

    if (!kc) {
        kc = kstat_open();
        if (!kc) return 0;
    }

    user = 0;
    sys = 0;
    wait = 0;
    idle = 0;

    for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
        if (strncmp(ksp->ks_name, "cpu_stat", 8) == 0) {
            if (kstat_read(kc, ksp, &cs) == -1) continue;

            user += cs.cpu_sysinfo.cpu[CPU_USER];
            sys += cs.cpu_sysinfo.cpu[CPU_KERNEL];
            wait += cs.cpu_sysinfo.cpu[CPU_WAIT];
            idle += cs.cpu_sysinfo.cpu[CPU_IDLE];
        }
    }

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
    static kstat_ctl_t *kc = NULL;
    kstat_t *ksp;
    cpu_stat_t cs;
    uint64_t user, sys, wait, idle;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;

    if (!kc) {
        kc = kstat_open();
        if (!kc) return 0;
    }

    user = 0;
    sys = 0;
    wait = 0;
    idle = 0;

    for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
        if (strncmp(ksp->ks_name, "cpu_stat", 8) == 0) {
            if (kstat_read(kc, ksp, &cs) == -1) continue;

            user += cs.cpu_sysinfo.cpu[CPU_USER];
            sys += cs.cpu_sysinfo.cpu[CPU_KERNEL];
            wait += cs.cpu_sysinfo.cpu[CPU_WAIT];
            idle += cs.cpu_sysinfo.cpu[CPU_IDLE];
        }
    }

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
    static kstat_ctl_t *kc = NULL;
    static long page_size = 0;
    static uint64_t total_pages = 0;
    kstat_t *ksp;
    kstat_named_t *k;
    uint64_t total_memory, free_memory, used_memory;

    if (!kc) {
        kc = kstat_open();
        if (!kc) return 0;
        page_size = sysconf(_SC_PAGESIZE);
        total_pages = sysconf(_SC_PHYS_PAGES);
    }

    total_memory = total_pages * page_size;

    ksp = kstat_lookup(kc, "unix", 0, "system_pages");
    if (!ksp) return 0;

    if (kstat_read(kc, ksp, NULL) == -1) return 0;

    k = (kstat_named_t *)kstat_data_lookup(ksp, "freemem");
    if (k) free_memory = k->value.ul * page_size;

    if (total_memory == 0) return 0;

    used_memory = total_memory - free_memory;
    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_loadavg_get_stats(double *value) {
    static kstat_ctl_t *kc = NULL;
    kstat_t *ksp;
    kstat_named_t *k;

    if (!kc) {
        kc = kstat_open();
        if (!kc) return 0;
    }

    ksp = kstat_lookup(kc, "unix", 0, "system_misc");
    if (!ksp) return 0;

    if (kstat_read(kc, ksp, NULL) == -1) return 0;

    k = (kstat_named_t *)kstat_data_lookup(ksp, "avenrun_1min");
    if (!k) return 0;

    *value = (double)k->value.l / (1l << 8);
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    static kstat_ctl_t *kc = NULL;
    kstat_t *ksp;
    kstat_named_t *k;
    unsigned int j;
    int found_rbytes, found_obytes;

    if (!kc) {
        kc = kstat_open();
        if (!kc) return 0;
    }

    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if (ksp->ks_type != KSTAT_TYPE_NAMED)
            continue;

        if (strcmp(ksp->ks_name, interface_name) != 0)
            continue;

        if (kstat_read(kc, ksp, NULL) == -1)
            continue;

        k = (kstat_named_t *)(ksp->ks_data);
        found_rbytes = 0;
        found_obytes = 0;

        for (j = 0; j < ksp->ks_ndata; j++, k++) {
            if (strncmp(k->name, "rbytes", 6) == 0) {
                *in_bytes = k->value.ul;
                found_rbytes = 1;
            } else if (strncmp(k->name, "obytes", 6) == 0) {
                *out_bytes = k->value.ul;
                found_obytes = 1;
            }
            if (found_rbytes && found_obytes)
                return 1;
        }
    }

    return 0;
}

void os_sleep(uint32_t milliseconds) {
    usleep(milliseconds * 1000);
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

    mutex->handle = malloc(sizeof(mutex_t));
    if (!mutex->handle) {
        free(mutex);
        return NULL;
    }

    ret = mutex_init((mutex_t*)mutex->handle, USYNC_THREAD, NULL);
    if (ret != 0) {
        free(mutex->handle);
        free(mutex);
        return NULL;
    }

    return mutex;
}

void os_plot_mutex_destroy(plot_mutex_t *mutex) {
    if (!mutex) return;

    mutex_destroy((mutex_t*)mutex->handle);
    free(mutex->handle);
    free(mutex);
}

void os_plot_mutex_lock(plot_mutex_t *mutex) {
    if (!mutex) return;

    mutex_lock((mutex_t*)mutex->handle);
}

void os_plot_mutex_unlock(plot_mutex_t *mutex) {
    if (!mutex) return;

    mutex_unlock((mutex_t*)mutex->handle);
}

plot_thread_t *os_plot_thread_create(void (*func)(void *), void *arg) {
    plot_thread_t *thread;
    int ret;

    thread = malloc(sizeof(plot_thread_t));
    if (!thread) return NULL;

    thread->handle = malloc(sizeof(thread_t));
    if (!thread->handle) {
        free(thread);
        return NULL;
    }

    ret = thr_create(NULL, 0, (void*(*)(void*))func, arg, 0, (thread_t*)thread->handle);
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

    thr_join(*(thread_t*)thread->handle, NULL, NULL);
}

int os_plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms) {
    int result;

    if (!thread) return 0;

    result = thr_join(*(thread_t*)thread->handle, NULL, NULL);
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
