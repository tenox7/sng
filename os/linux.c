#define _GNU_SOURCE
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
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

const char* os_get_platform_name(void) {
    return "linux";
}

int os_init(void) {
    return 1;
}

void os_cleanup(void) {
}

int os_cpu_get_stats(double *value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    
    FILE *fp;
    char line[256];
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;

    fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    if (fgets(line, sizeof(line), fp) &&
        sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {

        total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;

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
    fclose(fp);

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;
    
    FILE *fp;
    char line[256];
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;

    fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    if (fgets(line, sizeof(line), fp) &&
        sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {

        total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;
        system_ticks = system + irq + softirq;

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
    fclose(fp);

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    return 1;
}

int os_memory_get_stats(double *value) {
    FILE *fp;
    char line[256];
    uint64_t mem_total, mem_available;
    uint64_t total_memory, free_memory, used_memory;

    fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    mem_total = 0;
    mem_available = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
            total_memory = mem_total * 1024;
        } else if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) {
            free_memory = mem_available * 1024;
        }
    }
    fclose(fp);

    if (total_memory == 0) return 0;

    used_memory = total_memory - free_memory;
    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_loadavg_get_stats(double *value) {
    FILE *fp;
    char line[256];
    double loadavg1, loadavg5, loadavg15;

    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp) &&
            sscanf(line, "%lf %lf %lf", &loadavg1, &loadavg5, &loadavg15) >= 1) {
            *value = loadavg1;
        } else {
            *value = 0.0;
        }
        fclose(fp);
    } else {
        *value = 0.0;
    }
    if (*value < 0.0) *value = 0.0;

    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        return 0;
    }

    char line[256];
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        char ifname[32];
        unsigned long rx_bytes, tx_bytes;
        unsigned long rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        unsigned long tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

        if (sscanf(line, " %31[^:]: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   ifname, &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
                   &tx_bytes, &tx_packets, &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed) == 17) {

            if (strcmp(ifname, interface_name) == 0) {
                *in_bytes = (uint32_t)rx_bytes;
                *out_bytes = (uint32_t)tx_bytes;
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
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
    struct timespec ts;

    if (!thread) return 0;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }

    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    result = pthread_timedjoin_np(*(pthread_t*)thread->handle, NULL, &ts);
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
