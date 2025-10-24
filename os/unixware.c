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
#include <fcntl.h>
#include <nlist.h>
#include <sys/time.h>
#include <sys/select.h>
#include <thread.h>
#include <sys/mman.h>
#include <sys/dl.h>
#include <mas.h>
#include <metreg.h>

#ifndef USYNC_THREAD
#define USYNC_THREAD 0
#endif

#ifndef FSCALE
#define FSHIFT  8
#define FSCALE  (1<<FSHIFT)
#endif

#define UNIX_KERNEL "/stand/unix"
#define KMEM "/dev/kmem"
#define CPUSTATES 4

static int mas_fd = -1;
static uint32_t ncpu = 0;
static uint32_t cp_old[CPUSTATES];
static int mas_initialized = 0;

const char* os_get_platform_name(void) {
    return "unixware";
}

int os_init(void) {
    uint32_t *ncpu_p;
    int i;

    if (mas_initialized)
        return 1;

    mas_fd = mas_open(MAS_FILE, MAS_MMAP_ACCESS);
    if (mas_fd < 0) {
        return 0;
    }

    ncpu_p = (uint32_t *)mas_get_met(mas_fd, NCPU, 0);
    if (!ncpu_p) {
        mas_close(mas_fd);
        mas_fd = -1;
        return 0;
    }
    ncpu = (uint32_t)(*(short *)ncpu_p);

    for (i = 0; i < CPUSTATES; i++) {
        cp_old[i] = 0;
    }

    mas_initialized = 1;
    return 1;
}

void os_cleanup(void) {
    if (mas_fd >= 0) {
        mas_close(mas_fd);
        mas_fd = -1;
    }
    mas_initialized = 0;
}

static uint32_t get_cpu_metric(metid_t type) {
    uint32_t total;
    uint32_t *p;
    int i;

    total = 0;
    for (i = 0; i < ncpu; i++) {
        p = (uint32_t *)mas_get_met(mas_fd, type, i);
        if (p) {
            total += *p;
        }
    }
    return total;
}

static void calculate_cpu_percentages(uint32_t *new_vals, double *percentages) {
    uint32_t total_change;
    uint32_t half_total;
    int i;

    total_change = 0;
    for (i = 0; i < CPUSTATES; i++) {
        total_change += (new_vals[i] - cp_old[i]);
    }

    if (total_change == 0) {
        for (i = 0; i < CPUSTATES; i++) {
            percentages[i] = 0.0;
        }
        return;
    }

    half_total = total_change / 2;
    for (i = 0; i < CPUSTATES; i++) {
        percentages[i] = ((new_vals[i] - cp_old[i]) * 1000 + half_total) / total_change;
        percentages[i] = percentages[i] / 10.0;
        cp_old[i] = new_vals[i];
    }
}

int os_cpu_get_stats(double *value) {
    uint32_t cpu_states[CPUSTATES];
    double percentages[CPUSTATES];

    if (!mas_initialized || mas_fd < 0) {
        *value = 0.0;
        return 0;
    }

    cpu_states[0] = get_cpu_metric(MPC_CPU_IDLE);
    cpu_states[1] = get_cpu_metric(MPC_CPU_USR);
    cpu_states[2] = get_cpu_metric(MPC_CPU_SYS);
    cpu_states[3] = get_cpu_metric(MPC_CPU_WIO);

    calculate_cpu_percentages(cpu_states, percentages);

    *value = 100.0 - percentages[0];
    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    uint32_t cpu_states[CPUSTATES];
    double percentages[CPUSTATES];

    if (!mas_initialized || mas_fd < 0) {
        *total_value = 0.0;
        *system_value = 0.0;
        return 0;
    }

    cpu_states[0] = get_cpu_metric(MPC_CPU_IDLE);
    cpu_states[1] = get_cpu_metric(MPC_CPU_USR);
    cpu_states[2] = get_cpu_metric(MPC_CPU_SYS);
    cpu_states[3] = get_cpu_metric(MPC_CPU_WIO);

    calculate_cpu_percentages(cpu_states, percentages);

    *total_value = 100.0 - percentages[0];
    *system_value = percentages[2];
    return 1;
}

int os_memory_get_stats(double *value) {
    *value = 0.0;
    return 0;
}

int os_loadavg_get_stats(double *value) {
    static int kmem_fd = -1;
    static unsigned long avenrun_addr = 0;
    static int first_time = 1;
    long avenrun[3];

    if (first_time) {
        struct nlist nl[2];
        nl[0].n_name = "avenrun";
        nl[0].n_value = 0;
        nl[1].n_name = NULL;
        nl[1].n_value = 0;

        if (nlist(UNIX_KERNEL, nl) == 0 && nl[0].n_value != 0) {
            avenrun_addr = nl[0].n_value;
            kmem_fd = open(KMEM, O_RDONLY);
        }
        first_time = 0;
    }

    if (kmem_fd >= 0 && avenrun_addr != 0) {
        if (lseek(kmem_fd, avenrun_addr, SEEK_SET) != -1 &&
            read(kmem_fd, avenrun, sizeof(avenrun)) == sizeof(avenrun)) {
            *value = (double)avenrun[0] / FSCALE;
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
    FILE *fp;
    char line[256];
    char ifname[32];
    unsigned long rx_bytes, tx_bytes;
    unsigned long rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
    unsigned long tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        return 0;
    }

    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
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
