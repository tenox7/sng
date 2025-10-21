#include "os_interface.h"

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};
#include <sys/types.h>
#include <sys/table.h>
#include <mach.h>
#include <mach/mach_types.h>
#include <mach/vm_statistics.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>

static int pageshift = 0;

#define pagetok(size) ((size) << pageshift)

const char* os_get_platform_name(void) {
    return "decosf1";
}

int os_init(void) {
    int pagesize;

    pagesize = getpagesize();
    pageshift = 0;
    while (pagesize > 1) {
        pageshift++;
        pagesize >>= 1;
    }

    pageshift -= 10;

    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats(double *value) {
    static long old_cpu_ticks[5];
    static int first_time = 1;
    struct tbl_sysinfo sibuf;
    long new_ticks[5];
    long diff_ticks[5];
    long delta_ticks;
    int i;

    if (table(TBL_SYSINFO, 0, &sibuf, 1, sizeof(struct tbl_sysinfo)) < 0)
        return 0;

    new_ticks[0] = sibuf.si_user;
    new_ticks[1] = sibuf.si_nice;
    new_ticks[2] = sibuf.si_sys;
    new_ticks[3] = sibuf.wait;
    new_ticks[4] = sibuf.si_idle;

    if (first_time) {
        for (i = 0; i < 5; i++)
            old_cpu_ticks[i] = new_ticks[i];
        first_time = 0;
        *value = 0.0;
        return 1;
    }

    delta_ticks = 0;
    for (i = 0; i < 5; i++) {
        diff_ticks[i] = new_ticks[i] - old_cpu_ticks[i];
        delta_ticks += diff_ticks[i];
        old_cpu_ticks[i] = new_ticks[i];
    }

    if (delta_ticks == 0)
        delta_ticks = 1;

    *value = 100.0 * (1.0 - (double)diff_ticks[4] / delta_ticks);

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static long old_cpu_ticks[5];
    static int first_time = 1;
    struct tbl_sysinfo sibuf;
    long new_ticks[5];
    long diff_ticks[5];
    long delta_ticks;
    int i;

    if (table(TBL_SYSINFO, 0, &sibuf, 1, sizeof(struct tbl_sysinfo)) < 0)
        return 0;

    new_ticks[0] = sibuf.si_user;
    new_ticks[1] = sibuf.si_nice;
    new_ticks[2] = sibuf.si_sys;
    new_ticks[3] = sibuf.wait;
    new_ticks[4] = sibuf.si_idle;

    if (first_time) {
        for (i = 0; i < 5; i++)
            old_cpu_ticks[i] = new_ticks[i];
        first_time = 0;
        *total_value = 0.0;
        *system_value = 0.0;
        return 1;
    }

    delta_ticks = 0;
    for (i = 0; i < 5; i++) {
        diff_ticks[i] = new_ticks[i] - old_cpu_ticks[i];
        delta_ticks += diff_ticks[i];
        old_cpu_ticks[i] = new_ticks[i];
    }

    if (delta_ticks == 0)
        delta_ticks = 1;

    *total_value = 100.0 * (1.0 - (double)diff_ticks[4] / delta_ticks);
    *system_value = 100.0 * (double)diff_ticks[2] / delta_ticks;

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    return 1;
}

int os_memory_get_stats(double *value) {
    vm_statistics_data_t vmstats;
    uint64_t total_memory, used_memory;

    if (vm_statistics(task_self(), &vmstats) != KERN_SUCCESS)
        return 0;

    total_memory = vmstats.free_count + vmstats.active_count +
                   vmstats.inactive_count + vmstats.wire_count;

    if (total_memory == 0)
        return 0;

    used_memory = vmstats.active_count + vmstats.wire_count;

    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_loadavg_get_stats(double *value) {
    struct tbl_loadavg labuf;
    int i;

    if (table(TBL_LOADAVG, 0, &labuf, 1, sizeof(struct tbl_loadavg)) < 0)
        return 0;

    if (labuf.tl_lscale) {
        *value = (double)labuf.tl_avenrun.l[0] / (double)labuf.tl_lscale;
    } else {
        *value = labuf.tl_avenrun.d[0];
    }

    if (*value < 0.0)
        *value = 0.0;

    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
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
