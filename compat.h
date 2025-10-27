#ifndef COMPAT_H
#define COMPAT_H

/* Platform-specific feature test macros must come first */
#if defined(sgi) || defined(__sgi)
#define _SGI_SOURCE 1
#define _SGI_MP_SOURCE 1
#define _SGI_REENTRANT_FUNCTIONS 1
#endif

/* C89 compatibility definitions */

/* Include system headers first to get their definitions */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
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
