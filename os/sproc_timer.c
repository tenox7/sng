/*
 * Periodic timer for sproc "threads" (IRIX 5.3, no pthreads).
 *
 * Each collector is a real sproc process, so a plain nanosleep parks only that
 * process and needs no condition variable. Deadlines advance by a fixed step
 * to hold cadence; a large lag (clock jump) resyncs instead of bursting.
 */

#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

struct plot_timer_t {
    uint32_t interval_ms;
    uint64_t next_deadline_us;
};

static uint64_t plot_timer_now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

plot_timer_t *os_plot_timer_create(uint32_t interval_ms) {
    plot_timer_t *timer;

    if (interval_ms == 0) return NULL;

    timer = malloc(sizeof(plot_timer_t));
    if (!timer) return NULL;

    timer->interval_ms = interval_ms;
    timer->next_deadline_us = plot_timer_now_us() + (uint64_t)interval_ms * 1000ULL;
    return timer;
}

void os_plot_timer_destroy(plot_timer_t *timer) {
    if (!timer) return;
    free(timer);
}

void os_plot_timer_wait(plot_timer_t *timer) {
    uint64_t now;
    int64_t delta;
    uint64_t suspend_threshold;
    struct timespec ts;

    if (!timer) return;

    for (;;) {
        now = plot_timer_now_us();
        if (now >= timer->next_deadline_us) break;
        delta = (int64_t)(timer->next_deadline_us - now);
        ts.tv_sec = (time_t)(delta / 1000000);
        ts.tv_nsec = (long)((delta % 1000000) * 1000);
        if (nanosleep(&ts, NULL) == 0) break;
        if (errno != EINTR) break;
    }

    now = plot_timer_now_us();
    timer->next_deadline_us += (uint64_t)timer->interval_ms * 1000ULL;
    delta = (int64_t)timer->next_deadline_us - (int64_t)now;

    suspend_threshold = (uint64_t)timer->interval_ms * 4ULL * 1000ULL;
    if (suspend_threshold < 30000000ULL) suspend_threshold = 30000000ULL;

    if (-delta > (int64_t)suspend_threshold) {
        timer->next_deadline_us = now + (uint64_t)timer->interval_ms * 1000ULL;
    }
}
