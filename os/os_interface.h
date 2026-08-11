#ifndef OS_INTERFACE_H
#define OS_INTERFACE_H

#ifdef __VMS
#include "compat.h"  /* DEC C can't resolve "../" includes, uses /INCLUDE=[] */
#else
#include "../compat.h"
#endif
#include <stddef.h>
#include <string.h>

typedef struct plot_mutex_t plot_mutex_t;
typedef struct plot_thread_t plot_thread_t;
typedef struct plot_timer_t plot_timer_t;

/* CPU statistics functions - platform-specific implementations */
int os_cpu_get_stats(double *value);
int os_cpu_get_stats_dual(double *total_value, double *system_value);

/* Memory statistics. Primary is app memory (physical minus free minus the
 * reclaimable file cache) as a percent of physical; the free list alone pins at
 * ~100% once the page cache fills. Secondary is swap used as a percent of swap
 * total, 0.0 when there is no swap - store 0.0 rather than failing, or the ring
 * buffer stops advancing for the primary series too. */
int os_memory_get_stats(double *value);
int os_memory_get_stats_dual(double *used_value, double *swap_value);

#define OS_CLAMP_PCT(v) do { if ((v) > 100.0) (v) = 100.0; if ((v) < 0.0) (v) = 0.0; } while (0)

/* Load average functions */
int os_loadavg_get_stats(double *value);

/* Interface throughput functions - following existing pattern.
 * The pseudo interface "all" sums every non-loopback interface. Backends add
 * the raw counters modulo 2^32, which keeps the caller's unsigned delta
 * arithmetic correct as long as the aggregate delta stays under 4GB/sample. */
int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes);

#define IF_IS_ALL(n) ((n) && strcmp((n), "all") == 0)
#define IF_IS_LOOPBACK(n) ((n)[0] == 'l' && (n)[1] == 'o' && \
                           ((n)[2] == '\0' || ((n)[2] >= '0' && (n)[2] <= '9')))

/* Platform detection */
const char* os_get_platform_name(void);

/* Platform-specific initialization */
int os_init(void);
void os_cleanup(void);

/* Sleep function */
void os_sleep(uint32_t milliseconds);

/* Time function */
uint32_t os_get_time_ms(void);

/* Mutex functions */
plot_mutex_t *os_plot_mutex_create(void);
void os_plot_mutex_destroy(plot_mutex_t *mutex);
void os_plot_mutex_lock(plot_mutex_t *mutex);
void os_plot_mutex_unlock(plot_mutex_t *mutex);

/* Thread functions */
plot_thread_t *os_plot_thread_create(void (*func)(void *), void *arg);
void os_plot_thread_destroy(plot_thread_t *thread);
void os_plot_thread_join(plot_thread_t *thread);
int os_plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms);

/* Periodic timer (kernel-waitable, drift-corrected, suspend-aware) */
plot_timer_t *os_plot_timer_create(uint32_t interval_ms);
void os_plot_timer_destroy(plot_timer_t *timer);
void os_plot_timer_wait(plot_timer_t *timer);

/* Config path function */
char *os_get_config_path(const char *filename);

/* Ping functions */
typedef struct os_ping_context_t os_ping_context_t;
os_ping_context_t *os_ping_create(const char *hostname, uint32_t timeout_ms);
int os_ping_send(os_ping_context_t *ctx, double *ping_time_ms);
void os_ping_destroy(os_ping_context_t *ctx);

int os_get_default_gw_ip(char *buf, size_t buflen);

#endif /* OS_INTERFACE_H */
