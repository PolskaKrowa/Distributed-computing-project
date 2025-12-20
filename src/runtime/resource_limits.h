#ifndef RESOURCE_LIMITS_H
#define RESOURCE_LIMITS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime module: resource_limits.h
 *
 * Resource monitoring and enforcement for workers.
 * Tracks CPU, memory, and prevents resource exhaustion.
 *
 * Design:
 * - Monitor current resource usage
 * - Enforce soft and hard limits
 * - Graceful degradation on limit approach
 * - Per-worker resource accounting
 */

/* Resource limit handle (opaque) */
typedef struct resource_limits resource_limits_t;

/* Resource types */
typedef enum {
    RESOURCE_CPU_TIME = 0,
    RESOURCE_MEMORY = 1,
    RESOURCE_DISK_SPACE = 2,
    RESOURCE_OPEN_FILES = 3,
    RESOURCE_TASKS_RUNNING = 4
} resource_type_t;

/* Resource usage statistics */
typedef struct {
    /* CPU */
    double cpu_usage_percent;    /* Current CPU usage (0-100) */
    uint64_t cpu_time_us;        /* Total CPU time used (microseconds) */
    
    /* Memory */
    size_t memory_rss_bytes;     /* Resident set size */
    size_t memory_virt_bytes;    /* Virtual memory size */
    size_t memory_peak_bytes;    /* Peak memory usage */
    
    /* System */
    int num_threads;             /* Current thread count */
    int open_files;              /* Open file descriptors */
    
    /* Tasks */
    uint64_t tasks_completed;    /* Total tasks completed */
    uint64_t tasks_failed;       /* Total tasks failed */
    uint32_t tasks_running;      /* Currently executing */
} resource_stats_t;

/* Resource limit configuration */
typedef struct {
    /* Memory limits (bytes, 0 = unlimited) */
    size_t max_memory_bytes;
    size_t warn_memory_bytes;    /* Warning threshold */
    
    /* CPU limits */
    uint64_t max_cpu_time_us;    /* Per-task CPU time limit */
    double max_cpu_percent;      /* Overall CPU usage limit */
    
    /* Concurrency */
    uint32_t max_concurrent_tasks;
    
    /* File system */
    size_t min_disk_space_bytes;  /* Minimum free disk space */
    int max_open_files;
    
    /* Action on limit exceeded */
    int enforce_hard_limits;     /* 1 = terminate, 0 = warn only */
} resource_limits_config_t;

/* Limit check result */
typedef enum {
    LIMIT_OK = 0,
    LIMIT_WARNING = 1,           /* Approaching limit */
    LIMIT_EXCEEDED = 2,          /* Hard limit exceeded */
    LIMIT_CRITICAL = 3           /* Critical - immediate action needed */
} limit_status_t;

/*
 * Create resource limiter
 *
 * config: limit configuration (NULL = use defaults)
 *
 * Returns: limiter handle on success, NULL on failure
 */
resource_limits_t *resource_limits_create(const resource_limits_config_t *config);

/*
 * Destroy resource limiter
 */
void resource_limits_destroy(resource_limits_t *limits);

/*
 * Update resource usage statistics
 *
 * Should be called periodically to refresh measurements.
 * Returns: 0 on success, negative on error
 */
int resource_limits_update(resource_limits_t *limits);

/*
 * Check if resource is within limits
 *
 * type: resource type to check
 *
 * Returns: limit status
 */
limit_status_t resource_limits_check(resource_limits_t *limits, resource_type_t type);

/*
 * Check all resource limits
 *
 * Returns: worst status among all resources
 */
limit_status_t resource_limits_check_all(resource_limits_t *limits);

/*
 * Get current resource usage statistics
 */
void resource_limits_get_stats(resource_limits_t *limits, resource_stats_t *stats);

/*
 * Reserve resources for task execution
 *
 * Checks if resources are available for a new task.
 *
 * Returns: 0 if resources available, negative if insufficient
 */
int resource_limits_reserve_task(resource_limits_t *limits);

/*
 * Release resources after task completion
 *
 * task_success: 1 if task completed successfully, 0 if failed
 */
void resource_limits_release_task(resource_limits_t *limits, int task_success);

/*
 * Get memory usage for current process
 *
 * rss: output for resident set size (bytes)
 * virt: output for virtual memory size (bytes)
 *
 * Returns: 0 on success, negative on error
 */
int resource_limits_get_memory_usage(size_t *rss, size_t *virt);

/*
 * Get CPU time used by current process
 *
 * user_us: output for user CPU time (microseconds)
 * system_us: output for system CPU time (microseconds)
 *
 * Returns: 0 on success, negative on error
 */
int resource_limits_get_cpu_time(uint64_t *user_us, uint64_t *system_us);

/*
 * Get current CPU usage percentage
 *
 * Returns: CPU usage (0-100), or negative on error
 */
double resource_limits_get_cpu_percent(resource_limits_t *limits);

/*
 * Get available disk space
 *
 * path: path to check (e.g., ".")
 *
 * Returns: available bytes, or 0 on error
 */
uint64_t resource_limits_get_disk_space(const char *path);

/*
 * Get number of open file descriptors
 *
 * Returns: count, or negative on error
 */
int resource_limits_get_open_files(void);

/*
 * Set soft limit for resource
 *
 * Updates the warning threshold for a resource.
 */
int resource_limits_set_soft_limit(resource_limits_t *limits,
                                   resource_type_t type,
                                   uint64_t value);

/*
 * Set hard limit for resource
 *
 * Updates the maximum allowed value for a resource.
 */
int resource_limits_set_hard_limit(resource_limits_t *limits,
                                   resource_type_t type,
                                   uint64_t value);

/*
 * Reset resource usage counters
 */
void resource_limits_reset_stats(resource_limits_t *limits);

/*
 * Get human-readable status string
 */
const char *resource_limit_status_string(limit_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RESOURCE_LIMITS_H */