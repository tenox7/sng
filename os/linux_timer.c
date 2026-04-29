#include <sys/timerfd.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

struct plot_timer_t {
    int fd;
    uint32_t interval_ms;
    uint64_t next_deadline_ns;
};

static uint64_t plot_timer_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void plot_timer_arm(int fd, int64_t delta_ns) {
    struct itimerspec its;
    if (delta_ns < 1000000) delta_ns = 1000000;
    its.it_value.tv_sec = (time_t)(delta_ns / 1000000000LL);
    its.it_value.tv_nsec = (long)(delta_ns % 1000000000LL);
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    timerfd_settime(fd, 0, &its, NULL);
}

plot_timer_t *os_plot_timer_create(uint32_t interval_ms) {
    plot_timer_t *timer;

    if (interval_ms == 0) return NULL;

    timer = malloc(sizeof(plot_timer_t));
    if (!timer) return NULL;

    timer->fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer->fd < 0) {
        free(timer);
        return NULL;
    }

    timer->interval_ms = interval_ms;
    timer->next_deadline_ns = plot_timer_now_ns() + (uint64_t)interval_ms * 1000000ULL;
    plot_timer_arm(timer->fd, (int64_t)interval_ms * 1000000LL);
    return timer;
}

void os_plot_timer_destroy(plot_timer_t *timer) {
    if (!timer) return;
    close(timer->fd);
    free(timer);
}

void os_plot_timer_wait(plot_timer_t *timer) {
    uint64_t expirations;
    ssize_t n;
    uint64_t now_ns;
    int64_t delta_ns;
    uint64_t suspend_threshold_ns;

    if (!timer) return;

    do {
        n = read(timer->fd, &expirations, sizeof(expirations));
    } while (n < 0 && errno == EINTR);

    now_ns = plot_timer_now_ns();
    timer->next_deadline_ns += (uint64_t)timer->interval_ms * 1000000ULL;
    delta_ns = (int64_t)timer->next_deadline_ns - (int64_t)now_ns;

    suspend_threshold_ns = (uint64_t)timer->interval_ms * 4ULL * 1000000ULL;
    if (suspend_threshold_ns < 30000000000ULL) suspend_threshold_ns = 30000000000ULL;

    if (-delta_ns > (int64_t)suspend_threshold_ns) {
        timer->next_deadline_ns = now_ns + (uint64_t)timer->interval_ms * 1000000ULL;
        delta_ns = (int64_t)timer->interval_ms * 1000000LL;
    }

    plot_timer_arm(timer->fd, delta_ns);
}
