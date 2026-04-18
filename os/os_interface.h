#ifndef OS_INTERFACE_H
#define OS_INTERFACE_H

#include "../compat.h"
#include <stddef.h>

typedef struct plot_mutex_t plot_mutex_t;
typedef struct plot_thread_t plot_thread_t;

/* CPU statistics functions - platform-specific implementations */
int os_cpu_get_stats(double *value);
int os_cpu_get_stats_dual(double *total_value, double *system_value);

/* Memory statistics functions */
int os_memory_get_stats(double *value);

/* Load average functions */
int os_loadavg_get_stats(double *value);

/* Interface throughput functions - following existing pattern */
int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes);

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

/* Config path function */
char *os_get_config_path(const char *filename);

#if defined(__APPLE__) && defined(__MACH__)
/* Returns path to filename inside the .app bundle's Resources dir, or NULL
 * if not running from a bundle. Only meaningful on macOS. */
char *os_get_bundle_config_path(const char *filename);
#endif

/* Ping functions */
typedef struct os_ping_context_t os_ping_context_t;
os_ping_context_t *os_ping_create(const char *hostname, uint32_t timeout_ms);
int os_ping_send(os_ping_context_t *ctx, double *ping_time_ms);
void os_ping_destroy(os_ping_context_t *ctx);

/* Default gateway discovery. Writes dotted-quad IPv4 string into buf.
 * Returns 1 on success, 0 on failure. */
int os_get_default_gateway(char *buf, size_t buflen);

#endif /* OS_INTERFACE_H */
