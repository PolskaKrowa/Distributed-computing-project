/*
 * Distributed Computing Project - Scheduler
 * 
 * Task scheduling and worker management for the coordinator.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "worker_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scheduler handle (opaque) */
typedef struct scheduler scheduler_t;

/* Scheduling policies */
typedef enum {
    SCHED_POLICY_ROUND_ROBIN = 0,    /* Distribute evenly */
    SCHED_POLICY_LOAD_BALANCED = 1,  /* Prefer less-loaded workers */
    SCHED_POLICY_LOCALITY = 2,       /* Group related tasks */
    SCHED_POLICY_PRIORITY = 3        /* Honor task priorities */
} scheduling_policy_t;

/* Scheduler configuration */
typedef struct {
    size_t max_workers;
    int heartbeat_timeout_secs;
    scheduling_policy_t scheduling_policy;
} scheduler_config_t;

/* Task tracking info */
typedef struct {
    uint64_t task_id;
    uint32_t worker_id;
    time_t submitted;
    time_t started;
    int status;
} task_info_t;

/*
 * Create scheduler
 */
scheduler_t *scheduler_create(const scheduler_config_t *config);

/*
 * Destroy scheduler
 */
void scheduler_destroy(scheduler_t *sched);

/*
 * Register a new worker
 */
int scheduler_register_worker(scheduler_t *sched, const worker_info_t *info);

/*
 * Unregister a worker
 */
int scheduler_unregister_worker(scheduler_t *sched, uint32_t worker_id);

/*
 * Update worker heartbeat timestamp
 */
void scheduler_update_heartbeat(scheduler_t *sched, uint32_t worker_id);

/*
 * Mark worker as busy with n tasks
 */
void scheduler_mark_worker_busy(scheduler_t *sched, uint32_t worker_id, 
                                size_t task_count);

/*
 * Mark worker as idle
 */
void scheduler_mark_worker_idle(scheduler_t *sched, uint32_t worker_id);

/*
 * Get list of idle workers
 * 
 * Returns: number of idle workers found
 */
int scheduler_get_idle_workers(scheduler_t *sched, worker_info_t *workers,
                               size_t max_workers);

/*
 * Track a task assignment
 */
int scheduler_track_task(scheduler_t *sched, uint64_t task_id, 
                         uint32_t worker_id);

/*
 * Mark task as complete
 */
int scheduler_mark_task_complete(scheduler_t *sched, uint64_t task_id, 
                                 int status);

/*
 * Check for timed-out workers and mark them as dead
 * 
 * Returns: number of workers marked as dead
 */
int scheduler_check_timeouts(scheduler_t *sched);

/*
 * Get scheduler statistics
 */
typedef struct {
    size_t total_workers;
    size_t active_workers;
    size_t idle_workers;
    size_t busy_workers;
    size_t dead_workers;
    uint64_t tasks_scheduled;
    uint64_t tasks_completed;
    uint64_t tasks_failed;
} scheduler_stats_t;

void scheduler_get_stats(scheduler_t *sched, scheduler_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* SCHEDULER_H */