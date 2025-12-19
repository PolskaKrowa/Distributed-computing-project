#include "resource_limits.h"
#include <sys/resource.h>
#include <errno.h>
#include <stdio.h>
#include "common/log.h"

int resource_limits_apply(const resource_limits_t *limits)
{
    if (!limits) return -1;

    // Address space / virtual memory limit
    if (limits->max_address_space_bytes > 0) {
        struct rlimit rl;
        rl.rlim_cur = limits->max_address_space_bytes;
        rl.rlim_max = limits->max_address_space_bytes;
        if (setrlimit(RLIMIT_AS, &rl) != 0) {
            log_error("resource_limits: setrlimit(RLIMIT_AS) failed: %m");
            return -1;
        }
        log_debug("resource_limits: RLIMIT_AS set to %lu bytes", limits->max_address_space_bytes);
    }

    // CPU time (seconds)
    if (limits->max_cpu_seconds > 0) {
        struct rlimit rl;
        rl.rlim_cur = limits->max_cpu_seconds;
        rl.rlim_max = limits->max_cpu_seconds;
        if (setrlimit(RLIMIT_CPU, &rl) != 0) {
            log_error("resource_limits: setrlimit(RLIMIT_CPU) failed: %m");
            return -1;
        }
        log_debug("resource_limits: RLIMIT_CPU set to %lu seconds", limits->max_cpu_seconds);
    }

    // Open files
    if (limits->max_open_files > 0) {
        struct rlimit rl;
        rl.rlim_cur = limits->max_open_files;
        rl.rlim_max = limits->max_open_files;
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
            log_error("resource_limits: setrlimit(RLIMIT_NOFILE) failed: %m");
            return -1;
        }
        log_debug("resource_limits: RLIMIT_NOFILE set to %lu", limits->max_open_files);
    }

    return 0;
}

int resource_limits_format(const resource_limits_t *limits, char *buf, size_t buflen)
{
    if (!limits || !buf || buflen == 0) return -1;
    int n = snprintf(buf, buflen,
                     "address_space=%lu bytes, cpu_seconds=%lu, open_files=%lu",
                     limits->max_address_space_bytes,
                     limits->max_cpu_seconds,
                     limits->max_open_files);
    return (n < 0) ? -1 : 0;
}