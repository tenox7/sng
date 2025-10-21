#include "os_interface.h"

struct plot_mutex_t {
    void *handle;
};

struct plot_thread_t {
    void *handle;
};
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>

const char* os_get_platform_name(void) {
    return "freebsd";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats(double *value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;

    size_t size;
    long cp_time[5];
    uint64_t user, nice, system, interrupt, idle;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;

    size = sizeof(cp_time);
    if (sysctlbyname("kern.cp_time", cp_time, &size, NULL, 0) == 0) {
        user = cp_time[0];
        nice = cp_time[1];
        system = cp_time[2];
        interrupt = cp_time[3];
        idle = cp_time[4];

        total_ticks = user + nice + system + interrupt + idle;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;

            if (total_diff > 0) {
                *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
            } else {
                *value = 0.0;
            }
        } else {
            *value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
    }

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;

    size_t size;
    long cp_time[5];
    uint64_t user, nice, system, interrupt, idle;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;

    size = sizeof(cp_time);
    if (sysctlbyname("kern.cp_time", cp_time, &size, NULL, 0) == 0) {
        user = cp_time[0];
        nice = cp_time[1];
        system = cp_time[2];
        interrupt = cp_time[3];
        idle = cp_time[4];

        total_ticks = user + nice + system + interrupt + idle;
        system_ticks = system + interrupt;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;
            system_diff = system_ticks - prev_system;

            if (total_diff > 0) {
                *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
                *system_value = 100.0 * (double)system_diff / total_diff;
            } else {
                *total_value = 0.0;
                *system_value = 0.0;
            }
        } else {
            *total_value = 0.0;
            *system_value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
        prev_system = system_ticks;
    }

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    return 1;
}

int os_memory_get_stats(double *value) {
    size_t size;
    uint64_t free_pages, page_size, total_memory, free_memory, used_memory;

    size = sizeof(uint64_t);
    if (sysctlbyname("hw.physmem", &total_memory, &size, NULL, 0) != 0) {
        total_memory = 0;
    }

    free_pages = 0;
    page_size = 0;
    size = sizeof(free_pages);
    if (sysctlbyname("vm.stats.vm.v_free_count", &free_pages, &size, NULL, 0) == 0) {
        size = sizeof(page_size);
        if (sysctlbyname("vm.stats.vm.v_page_size", &page_size, &size, NULL, 0) == 0) {
            free_memory = free_pages * page_size;
        }
    }

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
#include "sryze-ping.c"

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
