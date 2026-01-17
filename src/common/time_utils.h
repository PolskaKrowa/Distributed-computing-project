#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <time.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef usleep()
#undef usleep()
#endif

/*
 * Portable sleep functions
 * 
 * usleep() is deprecated in POSIX.1-2008, so we provide
 * portable alternatives using nanosleep()
 */

/* Sleep for microseconds (portable replacement for usleep) */
static inline int usleep(unsigned long usec)
{
    struct timespec req, rem;
    
    req.tv_sec = usec / 1000000;
    req.tv_nsec = (usec % 1000000) * 1000;
    
    while (nanosleep(&req, &rem) == -1) {
        if (errno != EINTR) {
            return -1;
        }
        req = rem;
    }
    
    return 0;
}

/* Sleep for milliseconds */
static inline int sleep_ms(unsigned long msec)
{
    return usleep(msec * 1000);
}

#ifdef __cplusplus
}
#endif

#endif /* TIME_UTILS_H */