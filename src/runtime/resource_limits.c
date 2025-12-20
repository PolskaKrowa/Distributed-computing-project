#include "resource_limits.h"
#include "../common/log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <errno.h>

/* Default limits */
#define DEFAULT_MAX_MEMORY_MB 4096
#define DEFAULT_WARN_MEMORY_MB 3072
#define DEFAULT_MAX_CONCURRENT 8
#define DEFAULT_MIN_DISK_GB 10

/* Resource limiter structure */
struct resource_limits {
    /* Configuration */
    size_t max_memory_bytes;
    size_t warn_memory_bytes;
    uint64_t max_cpu_time_us;
    double max_cpu_percent;
    uint32_t max_concurrent_tasks;
    size_t min_disk_space_bytes;
    int max_open_files;
    int enforce_hard_limits;
    
    /* Current state */
    resource_stats_t stats;
    uint32_t tasks_running;
    
    /* CPU tracking for percentage calculation */
    struct rusage last_rusage;
    struct timespec last_update_time;
    double cpu_percent;
    
    /* Thread synchronization */
    pthread_mutex_t mutex;
};

/* Default configuration */
static const resource_limits_config_t default_config = {
    .max_memory_bytes = (size_t)DEFAULT_MAX_MEMORY_MB * 1024 * 1024,
    .warn_memory_bytes = (size_t)DEFAULT_WARN_MEMORY_MB * 1024 * 1024,
    .max_cpu_time_us = 0,  /* Unlimited per-task */
    .max_cpu_percent = 90.0,
    .max_concurrent_tasks = DEFAULT_MAX_CONCURRENT,
    .min_disk_space_bytes = (size_t)DEFAULT_MIN_DISK_GB * 1024 * 1024 * 1024,
    .max_open_files = 1024,
    .enforce_hard_limits = 1
};

resource_limits_t *resource_limits_create(const resource_limits_config_t *config)
{
    const resource_limits_config_t *cfg = config ? config : &default_config;
    
    resource_limits_t *limits = calloc(1, sizeof(*limits));
    if (!limits) {
        log_error("Failed to allocate resource limits");
        return NULL;
    }
    
    limits->max_memory_bytes = cfg->max_memory_bytes;
    limits->warn_memory_bytes = cfg->warn_memory_bytes;
    limits->max_cpu_time_us = cfg->max_cpu_time_us;
    limits->max_cpu_percent = cfg->max_cpu_percent;
    limits->max_concurrent_tasks = cfg->max_concurrent_tasks;
    limits->min_disk_space_bytes = cfg->min_disk_space_bytes;
    limits->max_open_files = cfg->max_open_files;
    limits->enforce_hard_limits = cfg->enforce_hard_limits;
    
    pthread_mutex_init(&limits->mutex, NULL);
    
    /* Initialize CPU tracking */
    getrusage(RUSAGE_SELF, &limits->last_rusage);
    clock_gettime(CLOCK_MONOTONIC, &limits->last_update_time);
    
    log_info("Created resource limits (max_memory=%.1f MB, max_concurrent=%u, enforce=%d)",
             limits->max_memory_bytes / (1024.0 * 1024.0),
             limits->max_concurrent_tasks,
             limits->enforce_hard_limits);
    
    return limits;
}

void resource_limits_destroy(resource_limits_t *limits)
{
    if (!limits) return;
    
    log_info("Destroying resource limits (tasks_completed=%lu, tasks_failed=%lu)",
             limits->stats.tasks_completed, limits->stats.tasks_failed);
    
    pthread_mutex_destroy(&limits->mutex);
    free(limits);
}

int resource_limits_get_memory_usage(size_t *rss, size_t *virt)
{
    if (!rss || !virt) return -1;
    
    /* Read from /proc/self/status on Linux */
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) {
        /* Fallback to rusage for RSS only */
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            *rss = usage.ru_maxrss * 1024;  /* Convert KB to bytes */
            *virt = 0;
            return 0;
        }
        return -1;
    }
    
    *rss = 0;
    *virt = 0;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            unsigned long kb;
            if (sscanf(line + 6, "%lu", &kb) == 1) {
                *rss = kb * 1024;
            }
        } else if (strncmp(line, "VmSize:", 7) == 0) {
            unsigned long kb;
            if (sscanf(line + 7, "%lu", &kb) == 1) {
                *virt = kb * 1024;
            }
        }
    }
    
    fclose(f);
    return 0;
}

int resource_limits_get_cpu_time(uint64_t *user_us, uint64_t *system_us)
{
    if (!user_us || !system_us) return -1;
    
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
    
    *user_us = (uint64_t)usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
    *system_us = (uint64_t)usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
    
    return 0;
}

double resource_limits_get_cpu_percent(resource_limits_t *limits)
{
    if (!limits) return -1.0;
    
    pthread_mutex_lock(&limits->mutex);
    double percent = limits->cpu_percent;
    pthread_mutex_unlock(&limits->mutex);
    
    return percent;
}

uint64_t resource_limits_get_disk_space(const char *path)
{
    if (!path) path = ".";
    
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
        log_error("Failed to get disk space for %s: %s", path, strerror(errno));
        return 0;
    }
    
    /* Available space in bytes */
    return (uint64_t)stat.f_bavail * stat.f_frsize;
}

int resource_limits_get_open_files(void)
{
    /* Count open file descriptors by reading /proc/self/fd */
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        /* Fallback: use getrlimit */
        struct rlimit rlim;
        if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
            /* Return current soft limit as approximation */
            return rlim.rlim_cur / 2;
        }
        return -1;
    }
    
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

int resource_limits_update(resource_limits_t *limits)
{
    if (!limits) return -1;
    
    pthread_mutex_lock(&limits->mutex);
    
    /* Update memory stats */
    size_t rss, virt;
    if (resource_limits_get_memory_usage(&rss, &virt) == 0) {
        limits->stats.memory_rss_bytes = rss;
        limits->stats.memory_virt_bytes = virt;
        if (rss > limits->stats.memory_peak_bytes) {
            limits->stats.memory_peak_bytes = rss;
        }
    }
    
    /* Update CPU stats */
    struct rusage current_usage;
    struct timespec current_time;
    
    if (getrusage(RUSAGE_SELF, &current_usage) == 0 &&
        clock_gettime(CLOCK_MONOTONIC, &current_time) == 0) {
        
        /* Calculate CPU time delta */
        uint64_t user_delta = 
            ((uint64_t)current_usage.ru_utime.tv_sec * 1000000 + current_usage.ru_utime.tv_usec) -
            ((uint64_t)limits->last_rusage.ru_utime.tv_sec * 1000000 + limits->last_rusage.ru_utime.tv_usec);
        
        uint64_t sys_delta = 
            ((uint64_t)current_usage.ru_stime.tv_sec * 1000000 + current_usage.ru_stime.tv_usec) -
            ((uint64_t)limits->last_rusage.ru_stime.tv_sec * 1000000 + limits->last_rusage.ru_stime.tv_usec);
        
        /* Calculate wall time delta */
        uint64_t wall_delta = 
            ((uint64_t)current_time.tv_sec * 1000000 + current_time.tv_nsec / 1000) -
            ((uint64_t)limits->last_update_time.tv_sec * 1000000 + limits->last_update_time.tv_nsec / 1000);
        
        if (wall_delta > 0) {
            /* CPU percentage = (CPU time / wall time) * 100 */
            limits->cpu_percent = ((double)(user_delta + sys_delta) / wall_delta) * 100.0;
            limits->stats.cpu_usage_percent = limits->cpu_percent;
        }
        
        /* Total CPU time */
        limits->stats.cpu_time_us = 
            (uint64_t)current_usage.ru_utime.tv_sec * 1000000 + current_usage.ru_utime.tv_usec +
            (uint64_t)current_usage.ru_stime.tv_sec * 1000000 + current_usage.ru_stime.tv_usec;
        
        limits->last_rusage = current_usage;
        limits->last_update_time = current_time;
    }
    
    /* Update file descriptor count */
    int open_fds = resource_limits_get_open_files();
    if (open_fds >= 0) {
        limits->stats.open_files = open_fds;
    }
    
    /* Update thread count (from /proc/self/status) */
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Threads:", 8) == 0) {
                int threads;
                if (sscanf(line + 8, "%d", &threads) == 1) {
                    limits->stats.num_threads = threads;
                }
                break;
            }
        }
        fclose(f);
    }
    
    limits->stats.tasks_running = limits->tasks_running;
    
    pthread_mutex_unlock(&limits->mutex);
    
    return 0;
}

limit_status_t resource_limits_check(resource_limits_t *limits, resource_type_t type)
{
    if (!limits) return LIMIT_OK;
    
    pthread_mutex_lock(&limits->mutex);
    
    limit_status_t status = LIMIT_OK;
    
    switch (type) {
        case RESOURCE_MEMORY:
            if (limits->max_memory_bytes > 0) {
                if (limits->stats.memory_rss_bytes >= limits->max_memory_bytes) {
                    status = LIMIT_EXCEEDED;
                    log_error("Memory limit exceeded: %.1f MB / %.1f MB",
                             limits->stats.memory_rss_bytes / (1024.0 * 1024.0),
                             limits->max_memory_bytes / (1024.0 * 1024.0));
                } else if (limits->stats.memory_rss_bytes >= limits->warn_memory_bytes) {
                    status = LIMIT_WARNING;
                    log_warn("Memory usage high: %.1f MB / %.1f MB",
                            limits->stats.memory_rss_bytes / (1024.0 * 1024.0),
                            limits->warn_memory_bytes / (1024.0 * 1024.0));
                }
            }
            break;
            
        case RESOURCE_CPU_TIME:
            if (limits->max_cpu_percent > 0) {
                if (limits->cpu_percent >= limits->max_cpu_percent) {
                    status = LIMIT_EXCEEDED;
                    log_error("CPU usage limit exceeded: %.1f%% / %.1f%%",
                             limits->cpu_percent, limits->max_cpu_percent);
                } else if (limits->cpu_percent >= limits->max_cpu_percent * 0.8) {
                    status = LIMIT_WARNING;
                }
            }
            break;
            
        case RESOURCE_TASKS_RUNNING:
            if (limits->tasks_running >= limits->max_concurrent_tasks) {
                status = LIMIT_EXCEEDED;
                log_warn("Maximum concurrent tasks reached: %u / %u",
                        limits->tasks_running, limits->max_concurrent_tasks);
            } else if (limits->tasks_running >= limits->max_concurrent_tasks * 0.8) {
                status = LIMIT_WARNING;
            }
            break;
            
        case RESOURCE_DISK_SPACE: {
            uint64_t available = resource_limits_get_disk_space(".");
            if (available > 0 && available < limits->min_disk_space_bytes) {
                status = LIMIT_EXCEEDED;
                log_error("Insufficient disk space: %.1f GB available",
                         available / (1024.0 * 1024.0 * 1024.0));
            } else if (available > 0 && available < limits->min_disk_space_bytes * 2) {
                status = LIMIT_WARNING;
            }
            break;
        }
            
        case RESOURCE_OPEN_FILES:
            if (limits->max_open_files > 0 && 
                limits->stats.open_files >= limits->max_open_files) {
                status = LIMIT_EXCEEDED;
                log_error("Too many open files: %d / %d",
                         limits->stats.open_files, limits->max_open_files);
            } else if (limits->max_open_files > 0 &&
                      limits->stats.open_files >= limits->max_open_files * 0.8) {
                status = LIMIT_WARNING;
            }
            break;
    }
    
    pthread_mutex_unlock(&limits->mutex);
    
    return status;
}

limit_status_t resource_limits_check_all(resource_limits_t *limits)
{
    if (!limits) return LIMIT_OK;
    
    /* Update stats first */
    resource_limits_update(limits);
    
    limit_status_t worst = LIMIT_OK;
    
    limit_status_t statuses[] = {
        resource_limits_check(limits, RESOURCE_MEMORY),
        resource_limits_check(limits, RESOURCE_CPU_TIME),
        resource_limits_check(limits, RESOURCE_TASKS_RUNNING),
        resource_limits_check(limits, RESOURCE_DISK_SPACE),
        resource_limits_check(limits, RESOURCE_OPEN_FILES)
    };
    
    for (int i = 0; i < 5; i++) {
        if (statuses[i] > worst) {
            worst = statuses[i];
        }
    }
    
    return worst;
}

void resource_limits_get_stats(resource_limits_t *limits, resource_stats_t *stats)
{
    if (!limits || !stats) return;
    
    pthread_mutex_lock(&limits->mutex);
    memcpy(stats, &limits->stats, sizeof(resource_stats_t));
    pthread_mutex_unlock(&limits->mutex);
}

int resource_limits_reserve_task(resource_limits_t *limits)
{
    if (!limits) return -1;
    
    pthread_mutex_lock(&limits->mutex);
    
    /* Check if we can accept another task */
    if (limits->tasks_running >= limits->max_concurrent_tasks) {
        pthread_mutex_unlock(&limits->mutex);
        log_debug("Cannot reserve task: max concurrent reached (%u)",
                 limits->max_concurrent_tasks);
        return -1;
    }
    
    /* Check memory */
    if (limits->max_memory_bytes > 0 &&
        limits->stats.memory_rss_bytes >= limits->max_memory_bytes * 0.9) {
        pthread_mutex_unlock(&limits->mutex);
        log_warn("Cannot reserve task: memory usage too high");
        return -2;
    }
    
    limits->tasks_running++;
    
    pthread_mutex_unlock(&limits->mutex);
    
    log_debug("Reserved resources for task (running=%u)", limits->tasks_running);
    return 0;
}

void resource_limits_release_task(resource_limits_t *limits, int task_success)
{
    if (!limits) return;
    
    pthread_mutex_lock(&limits->mutex);
    
    if (limits->tasks_running > 0) {
        limits->tasks_running--;
    }
    
    if (task_success) {
        limits->stats.tasks_completed++;
    } else {
        limits->stats.tasks_failed++;
    }
    
    pthread_mutex_unlock(&limits->mutex);
    
    log_debug("Released resources (running=%u, completed=%lu, failed=%lu)",
             limits->tasks_running,
             limits->stats.tasks_completed,
             limits->stats.tasks_failed);
}

int resource_limits_set_soft_limit(resource_limits_t *limits,
                                   resource_type_t type,
                                   uint64_t value)
{
    if (!limits) return -1;
    
    pthread_mutex_lock(&limits->mutex);
    
    switch (type) {
        case RESOURCE_MEMORY:
            limits->warn_memory_bytes = value;
            log_info("Set memory warning threshold: %.1f MB", value / (1024.0 * 1024.0));
            break;
        default:
            pthread_mutex_unlock(&limits->mutex);
            return -1;
    }
    
    pthread_mutex_unlock(&limits->mutex);
    return 0;
}

int resource_limits_set_hard_limit(resource_limits_t *limits,
                                   resource_type_t type,
                                   uint64_t value)
{
    if (!limits) return -1;
    
    pthread_mutex_lock(&limits->mutex);
    
    switch (type) {
        case RESOURCE_MEMORY:
            limits->max_memory_bytes = value;
            log_info("Set memory hard limit: %.1f MB", value / (1024.0 * 1024.0));
            break;
        case RESOURCE_TASKS_RUNNING:
            limits->max_concurrent_tasks = (uint32_t)value;
            log_info("Set max concurrent tasks: %u", limits->max_concurrent_tasks);
            break;
        default:
            pthread_mutex_unlock(&limits->mutex);
            return -1;
    }
    
    pthread_mutex_unlock(&limits->mutex);
    return 0;
}

void resource_limits_reset_stats(resource_limits_t *limits)
{
    if (!limits) return;
    
    pthread_mutex_lock(&limits->mutex);
    
    limits->stats.tasks_completed = 0;
    limits->stats.tasks_failed = 0;
    limits->stats.memory_peak_bytes = limits->stats.memory_rss_bytes;
    
    pthread_mutex_unlock(&limits->mutex);
    
    log_info("Reset resource statistics");
}

const char *resource_limit_status_string(limit_status_t status)
{
    switch (status) {
        case LIMIT_OK:       return "OK";
        case LIMIT_WARNING:  return "WARNING";
        case LIMIT_EXCEEDED: return "EXCEEDED";
        case LIMIT_CRITICAL: return "CRITICAL";
        default:             return "UNKNOWN";
    }
}