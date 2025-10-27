#if defined(__APPLE__) && defined(__MACH__)
    #include "darwin.c"
#elif defined(__linux__)
    #include "linux.c"
#elif defined(__FreeBSD__)
    #include "freebsd.c"
#elif defined(__sun) || defined(__sun__) || defined(sun)
    #include "sunos.c"
#elif defined(__hpux) || defined(hpux) || defined(_HPUX_SOURCE)
    #include "hpux.c"
#elif defined(_AIX)
    #include "aix.c"
#elif defined(UNIXWARE) || defined(__USLC__)
    #include "unixware.c"
#elif defined(sgi) || defined(__sgi)
    #include "irix65.c"
#elif defined(__osf__) || defined(__OSF1__)
    #include "decosf1.c"
#else
    #include "unix.c"
#endif
