#include "os_interface.h"
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/vm_statistics.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>

#include "sryze-ping.c"

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};

const char* os_get_platform_name(void) {
    return "darwin";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats(double *value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;

    processor_info_array_t cpu_info;
    mach_msg_type_number_t num_cpu_info;
    natural_t num_processors;
    uint64_t idle, total;
    natural_t i;
    processor_cpu_load_info_t cpu_load;
    uint64_t idle_diff, total_diff;

    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                           &num_processors, &cpu_info, &num_cpu_info) != KERN_SUCCESS) {
        return 0;
    }

    idle = 0;
    total = 0;
    for (i = 0; i < num_processors; i++) {
        cpu_load = (processor_cpu_load_info_t)cpu_info;
        idle += cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
        total += cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                cpu_load[i].cpu_ticks[CPU_STATE_NICE];
    }

    if (prev_total != 0) {
        idle_diff = idle - prev_idle;
        total_diff = total - prev_total;
        *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
    } else {
        *value = 0.0;
    }

    prev_idle = idle;
    prev_total = total;

    vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                  num_cpu_info * sizeof(integer_t));

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;

    processor_info_array_t cpu_info;
    mach_msg_type_number_t num_cpu_info;
    natural_t num_processors;
    uint64_t idle, total, system;
    natural_t i;
    processor_cpu_load_info_t cpu_load;
    uint64_t idle_diff, total_diff, system_diff;

    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                           &num_processors, &cpu_info, &num_cpu_info) != KERN_SUCCESS) {
        return 0;
    }

    idle = 0;
    total = 0;
    system = 0;
    for (i = 0; i < num_processors; i++) {
        cpu_load = (processor_cpu_load_info_t)cpu_info;
        idle += cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
        system += cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM];
        total += cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                cpu_load[i].cpu_ticks[CPU_STATE_NICE];
    }

    if (prev_total != 0) {
        idle_diff = idle - prev_idle;
        total_diff = total - prev_total;
        system_diff = system - prev_system;

        *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
        *system_value = 100.0 * (double)system_diff / total_diff;
    } else {
        *total_value = 0.0;
        *system_value = 0.0;
    }

    prev_idle = idle;
    prev_total = total;
    prev_system = system;

    vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                  num_cpu_info * sizeof(integer_t));

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    return 1;
}

int os_memory_get_stats(double *value) {
    int mib[2];
    size_t len;
    uint64_t total_memory;
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count;
    uint64_t free_memory, used_memory;

    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    len = sizeof(total_memory);
    if (sysctl(mib, 2, &total_memory, &len, NULL, 0) != 0) {
        return 0;
    }

    count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         (host_info64_t)&vm_stats, &count) != KERN_SUCCESS) {
        return 0;
    }

    free_memory = vm_stats.free_count * 4096;

    if (total_memory == 0) return 0;

    used_memory = total_memory - free_memory;
    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_loadavg_get_stats(double *value) {
    double loadavg[3];

    if (getloadavg(loadavg, 3) != -1) {
        *value = loadavg[0];
    } else {
        *value = 0.0;
    }
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    int mib[6];
    size_t len;
    char *buf, *next, *lim;
    struct if_msghdr *ifm;
    struct sockaddr_dl *sdl;

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = 0;
    mib[4] = NET_RT_IFLIST;
    mib[5] = 0;

    if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) {
        return 0;
    }

    buf = malloc(len);
    if (!buf) {
        return 0;
    }

    if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
        free(buf);
        return 0;
    }

    lim = buf + len;
    for (next = buf; next < lim;) {
        ifm = (struct if_msghdr *)next;

        if (ifm->ifm_type == RTM_IFINFO) {
            sdl = (struct sockaddr_dl *)(ifm + 1);
            if (sdl->sdl_family == AF_LINK) {
                char ifname[32];
                memcpy(ifname, sdl->sdl_data, sdl->sdl_nlen);
                ifname[sdl->sdl_nlen] = '\0';

                if (strcmp(ifname, interface_name) == 0) {
                    *in_bytes = ifm->ifm_data.ifi_ibytes;
                    *out_bytes = ifm->ifm_data.ifi_obytes;
                    free(buf);
                    return 1;
                }
            }
        }

        next += ifm->ifm_msglen;
    }

    free(buf);
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

    home = getenv("HOME");
    if (!home) return NULL;

    snprintf(config_path, sizeof(config_path), "%s/Library/Preferences/%s", home, filename);

    return config_path;
}

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