#ifndef RUNTIME_RESOURCE_LIMITS_H
#define RUNTIME_RESOURCE_LIMITS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

typedef struct resource_limits {
    /** Maximum address space in bytes (0 = no limit) */
    unsigned long max_address_space_bytes;
    /** Maximum CPU time in seconds (0 = no limit) */
    unsigned long max_cpu_seconds;
    /** Maximum number of open file descriptors (0 = no limit) */
    unsigned long max_open_files;
} resource_limits_t;

/**
 * Apply the provided resource limits to the current process.
 * Returns 0 on success, -1 on error (errno set).
 * This function is Linux/Unix specific and uses setrlimit.
 */
int resource_limits_apply(const resource_limits_t *limits);

/** Convert limits to a human readable string placed into the provided buffer. */
int resource_limits_format(const resource_limits_t *limits, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_RESOURCE_LIMITS_H