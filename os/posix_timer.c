#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

struct plot_timer_t {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
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

    if (pthread_mutex_init(&timer->mutex, NULL) != 0) {
        free(timer);
        return NULL;
    }

    if (pthread_cond_init(&timer->cond, NULL) != 0) {
        pthread_mutex_destroy(&timer->mutex);
        free(timer);
        return NULL;
    }

    timer->interval_ms = interval_ms;
    timer->next_deadline_us = plot_timer_now_us() + (uint64_t)interval_ms * 1000ULL;
    return timer;
}

void os_plot_timer_destroy(plot_timer_t *timer) {
    if (!timer) return;
    pthread_cond_destroy(&timer->cond);
    pthread_mutex_destroy(&timer->mutex);
    free(timer);
}

void os_plot_timer_wait(plot_timer_t *timer) {
    struct timespec ts;
    uint64_t now;
    int64_t delta;
    uint64_t suspend_threshold;
    int rc;

    if (!timer) return;

    pthread_mutex_lock(&timer->mutex);

    ts.tv_sec = (time_t)(timer->next_deadline_us / 1000000ULL);
    ts.tv_nsec = (long)((timer->next_deadline_us % 1000000ULL) * 1000ULL);

    do {
        rc = pthread_cond_timedwait(&timer->cond, &timer->mutex, &ts);
    } while (rc != ETIMEDOUT && plot_timer_now_us() < timer->next_deadline_us);

    now = plot_timer_now_us();
    timer->next_deadline_us += (uint64_t)timer->interval_ms * 1000ULL;
    delta = (int64_t)timer->next_deadline_us - (int64_t)now;

    suspend_threshold = (uint64_t)timer->interval_ms * 4ULL * 1000ULL;
    if (suspend_threshold < 30000000ULL) suspend_threshold = 30000000ULL;

    if (-delta > (int64_t)suspend_threshold) {
        timer->next_deadline_us = now + (uint64_t)timer->interval_ms * 1000ULL;
    }

    pthread_mutex_unlock(&timer->mutex);
}
