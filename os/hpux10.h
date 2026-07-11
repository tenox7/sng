#ifndef HPUX10_H
#define HPUX10_H

/*
 * HP-UX 10.20 has no POSIX threads: no pthread.h, no libpthread. The only
 * thread library is DCE/CMA (libcma), which implements POSIX draft 4 and
 * ships without headers here, so declare what we use.
 *
 * Force-included into every translation unit (-include os/hpux10.h) because
 * the I/O jackets below must be visible to the datasources, not just to
 * os/hpux.c. Link with gcc -threads (-lcma -lc_r).
 */

/* Pull in everything that prototypes a call jacketed below: the macros must
 * come after those declarations or they would rewrite the declarations too. */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

/* Every CMA object is this opaque handle. Draft 4 passes them by value, so
 * the layout has to match libcma exactly (8 bytes). */
typedef struct { void *f1; short f2; short f3; } cma_handle_t;

typedef cma_handle_t pthread_t;
typedef cma_handle_t pthread_attr_t;
typedef cma_handle_t pthread_mutex_t;
typedef cma_handle_t pthread_mutexattr_t;
typedef void *pthread_addr_t;
typedef void *(*pthread_startroutine_t)(void *);

extern pthread_attr_t pthread_attr_default;
extern pthread_mutexattr_t pthread_mutexattr_default;

extern int pthread_create(pthread_t *, pthread_attr_t, pthread_startroutine_t, pthread_addr_t);
extern int pthread_join(pthread_t, pthread_addr_t *);
extern int pthread_detach(pthread_t *);
extern int pthread_mutex_init(pthread_mutex_t *, pthread_mutexattr_t);
extern int pthread_mutex_destroy(pthread_mutex_t *);
extern int pthread_mutex_lock(pthread_mutex_t *);
extern int pthread_mutex_unlock(pthread_mutex_t *);
extern int pthread_delay_np(struct timespec *);

/*
 * CMA multiplexes all threads onto one LWP, so a bare blocking syscall stops
 * every thread, GUI included. libcma exports jacketed equivalents that yield
 * instead; route the blocking calls through them. cma_close() matters too:
 * CMA keeps per-fd state and the datasources cycle sockets on every poll.
 *
 * Function-like so plain identifiers (a struct field named read) are left
 * alone, and declared without prototypes to sidestep HP's pre-XPG argument
 * types (int vs size_t, int * vs fd_set *).
 */
extern int cma_select(), cma_socket(), cma_connect(), cma_close();
extern int cma_read(), cma_write(), cma_recv(), cma_recvfrom(), cma_send(), cma_sendto();
extern int cma_nanosleep();

#define select(n, r, w, e, t)      cma_select((n), (r), (w), (e), (t))
#define socket(d, t, p)            cma_socket((d), (t), (p))
#define connect(s, a, l)           cma_connect((s), (a), (l))
#define close(f)                   cma_close((f))
#define read(f, b, n)              cma_read((f), (b), (n))
#define write(f, b, n)             cma_write((f), (b), (n))
#define recv(s, b, n, f)           cma_recv((s), (b), (n), (f))
#define recvfrom(s, b, n, f, a, l) cma_recvfrom((s), (b), (n), (f), (a), (l))
#define send(s, b, n, f)           cma_send((s), (b), (n), (f))
#define sendto(s, b, n, f, a, l)   cma_sendto((s), (b), (n), (f), (a), (l))
#define nanosleep(req, rem)        cma_nanosleep((req), (rem))

#endif /* HPUX10_H */
