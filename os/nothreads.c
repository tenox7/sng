/* Single-threaded build (SNG_NO_THREADS) for systems with no thread library,
 * such as HP-UX 9. threading.c samples the sources from the main loop, so
 * mutexes do nothing and threads cannot be created (the web server, which
 * needs its own thread, does not start). */

#include <stdlib.h>

struct plot_timer_t {
    uint32_t interval_ms;
    uint32_t next_ms;
};

plot_mutex_t *os_plot_mutex_create(void) {
    return malloc(sizeof(plot_mutex_t));
}

void os_plot_mutex_destroy(plot_mutex_t *mutex) {
    free(mutex);
}

void os_plot_mutex_lock(plot_mutex_t *mutex) {
    (void)mutex;
}

void os_plot_mutex_unlock(plot_mutex_t *mutex) {
    (void)mutex;
}

plot_thread_t *os_plot_thread_create(void (*func)(void *), void *arg) {
    (void)func;
    (void)arg;
    return NULL;
}

void os_plot_thread_destroy(plot_thread_t *thread) {
    free(thread);
}

void os_plot_thread_join(plot_thread_t *thread) {
    (void)thread;
}

int os_plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms) {
    (void)thread;
    (void)timeout_ms;
    return 1;
}

plot_timer_t *os_plot_timer_create(uint32_t interval_ms) {
    plot_timer_t *timer;

    if (interval_ms == 0) return NULL;
    timer = malloc(sizeof(plot_timer_t));
    if (!timer) return NULL;
    timer->interval_ms = interval_ms;
    timer->next_ms = os_get_time_ms() + interval_ms;
    return timer;
}

void os_plot_timer_destroy(plot_timer_t *timer) {
    free(timer);
}

void os_plot_timer_wait(plot_timer_t *timer) {
    int32_t left;

    if (!timer) return;
    left = (int32_t)(timer->next_ms - os_get_time_ms());
    if (left > 0) os_sleep((uint32_t)left);
    timer->next_ms += timer->interval_ms;
}
