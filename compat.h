#ifndef COMPAT_H
#define COMPAT_H

/* Platform-specific feature test macros must come first */
#if defined(sgi) || defined(__sgi)
#define _SGI_SOURCE 1
#define _SGI_MP_SOURCE 1
#define _SGI_REENTRANT_FUNCTIONS 1
#endif

#if defined(_WIN32) && defined(_MSC_VER) && _MSC_VER < 1900
#include <stdio.h>
#define snprintf _snprintf
#endif

#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

/* C89 compatibility definitions */

/* Include system headers first to get their definitions */
/* VMS checks must precede __DECC: long is 32-bit on OpenVMS */
#if defined(__VMS) && defined(__VAX)
  /* VAX has no 64-bit integer type; uint64_t here is only 32 bits wide.
   * All current uses are wrap-safe ms differences or small sums.
   * Claim the inttypes.h guard: the system version typedefs int64_t as a
   * two-int struct, which would clash with these when socket headers pull
   * it in. */
  #define __INTTYPES_LOADED 1
  #include <sys/types.h>
  typedef unsigned char uint8_t;
  typedef unsigned short uint16_t;
  typedef unsigned int uint32_t;
  typedef unsigned long uint64_t;
  typedef signed char int8_t;
  typedef short int16_t;
  typedef int int32_t;
  typedef long int64_t;
  #ifndef UINT32_MAX
    #define UINT32_MAX 4294967295U
  #endif
  #ifndef INT32_MAX
    #define INT32_MAX 2147483647
  #endif
  #ifndef INT32_MIN
    #define INT32_MIN (-2147483647-1)
  #endif
#elif defined(__VMS)
  #include <inttypes.h>
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
  #include <stdint.h>
#elif defined(_AIX)
  #include <sys/types.h>
  #include <sys/limits.h>
  typedef unsigned char uint8_t;
  typedef unsigned short uint16_t;
  typedef unsigned int uint32_t;
  typedef unsigned long long uint64_t;
  typedef signed char int8_t;
  typedef short int16_t;
  typedef int int32_t;
  typedef long long int64_t;
  #ifndef INET_ADDRSTRLEN
    #define INET_ADDRSTRLEN 16
  #endif
  #ifndef UINT32_MAX
    #define UINT32_MAX UINT_MAX
  #endif
  #ifndef INT32_MAX
    #define INT32_MAX INT_MAX
  #endif
  #ifndef INT32_MIN
    #define INT32_MIN INT_MIN
  #endif
#elif defined(__sun) || defined(__sun__) || defined(sun)
  #include <sys/types.h>
  #include <inttypes.h>
#elif defined(__hpux)
  #include <sys/types.h>
  #include <inttypes.h>
#elif defined(sgi)
  #include <sys/types.h>
  #include <inttypes.h>
#elif defined(__osf__) || defined(__digital__) || defined(__DECC)
  #include <sys/types.h>
  typedef unsigned char uint8_t;
  typedef unsigned short uint16_t;
  typedef unsigned int uint32_t;
  typedef unsigned long uint64_t;
  typedef signed char int8_t;
  typedef short int16_t;
  typedef int int32_t;
  typedef long int64_t;
  #ifndef UINT32_MAX
    #define UINT32_MAX 4294967295U
  #endif
  #ifndef INT32_MAX
    #define INT32_MAX 2147483647
  #endif
  #ifndef INT32_MIN
    #define INT32_MIN (-2147483647-1)
  #endif
#else
  #include <stdint.h>
#endif

/* Integer types - only define if not already defined by system */
/* Note: On systems with stdint.h, these types are already defined */

/* Ensure UINT32_MAX is defined for all platforms */
#ifndef UINT32_MAX
#define UINT32_MAX 4294967295U
#endif

/* VMS CRTL gained snprintf in 7.3-2; fall back to vsprintf (unbounded) */
#if defined(__VMS) && defined(__CRTL_VER) && (__CRTL_VER < 70312000)
  #include <stddef.h>
  int sng_snprintf(char *buf, size_t size, const char *fmt, ...);
  #define snprintf sng_snprintf
#endif

/* Atomic operations - use regular types for maximum compatibility */
#ifndef atomic_uint_fast32_t
#define atomic_uint_fast32_t uint32_t
#endif

#ifndef atomic_load
#define atomic_load(ptr) (*(ptr))
#endif

#ifndef atomic_store
#define atomic_store(ptr, val) (*(ptr) = (val))
#endif

#endif /* COMPAT_H */
