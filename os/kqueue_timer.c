#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

struct plot_timer_t {
    int kq;
    uint32_t interval_ms;
    uint64_t next_deadline_us;
};

static uint64_t plot_timer_now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void plot_timer_arm(int kq, intptr_t delay_ms) {
    struct kevent kev;
    if (delay_ms < 1) delay_ms = 1;
    EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, delay_ms, NULL);
    kevent(kq, &kev, 1, NULL, 0, NULL);
}

plot_timer_t *os_plot_timer_create(uint32_t interval_ms) {
    plot_timer_t *timer;

    if (interval_ms == 0) return NULL;

    timer = malloc(sizeof(plot_timer_t));
    if (!timer) return NULL;

    timer->kq = kqueue();
    if (timer->kq < 0) {
        free(timer);
        return NULL;
    }

    timer->interval_ms = interval_ms;
    timer->next_deadline_us = plot_timer_now_us() + (uint64_t)interval_ms * 1000ULL;
    plot_timer_arm(timer->kq, (intptr_t)interval_ms);
    return timer;
}

void os_plot_timer_destroy(plot_timer_t *timer) {
    if (!timer) return;
    close(timer->kq);
    free(timer);
}

void os_plot_timer_wait(plot_timer_t *timer) {
    struct kevent kev;
    int n;
    uint64_t now_us;
    int64_t delta_us;
    uint64_t suspend_threshold_us;
    intptr_t delay_ms;

    if (!timer) return;

    do {
        n = kevent(timer->kq, NULL, 0, &kev, 1, NULL);
    } while (n < 0 && errno == EINTR);

    now_us = plot_timer_now_us();
    timer->next_deadline_us += (uint64_t)timer->interval_ms * 1000ULL;
    delta_us = (int64_t)timer->next_deadline_us - (int64_t)now_us;

    suspend_threshold_us = (uint64_t)timer->interval_ms * 4ULL * 1000ULL;
    if (suspend_threshold_us < 30000000ULL) suspend_threshold_us = 30000000ULL;

    if (-delta_us > (int64_t)suspend_threshold_us) {
        timer->next_deadline_us = now_us + (uint64_t)timer->interval_ms * 1000ULL;
        delay_ms = (intptr_t)timer->interval_ms;
    } else if (delta_us <= 0) {
        delay_ms = 1;
    } else {
        delay_ms = (intptr_t)((delta_us + 999) / 1000);
    }

    plot_timer_arm(timer->kq, delay_ms);
}
