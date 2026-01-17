#ifndef WORKER_H
#define WORKER_H

#include <stdint.h>
#include <stddef.h>
#include "../../include/task.h"
#include "../../include/result.h"
#include "../../include/fortran_api.h"
#include "../net/transport.h"
#include "../runtime/resource_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Worker module: worker.h
 *
 * Distributed computing worker that executes Fortran computational kernels.
 *
 * Supports three modes:
 * 1. Regular worker: connects to external coordinator
 * 2. Local worker: part of MPI cluster
 * 3. Master-worker: orchestrates local MPI cluster AND connects to external coordinator
 *
 * Design:
 * - Executes tasks by calling Fortran API
 * - Monitors resource usage
 * - Sends heartbeats to coordinator
 * - Supports task cancellation
 * - Can manage sub-workers in master mode
 */

/* Worker handle (opaque) */
typedef struct worker worker_t;

/* Worker mode */
typedef enum {
    WORKER_MODE_STANDALONE,         /* Single worker */
    WORKER_MODE_LOCAL_MPI,          /* Part of local MPI cluster */
    WORKER_MODE_MASTER              /* Master worker with sub-workers */
} worker_mode_t;

/* Worker configuration */
typedef struct {
    /* Transport configuration */
    transport_type_t transport_type;
    const char *coordinator_endpoint;
    int mpi_rank;                   /* For MPI workers */
    
    /* Mode */
    worker_mode_t mode;
    
    /* Master-worker specific */
    int num_sub_workers;            /* Number of local MPI workers */
    transport_t *local_transport;   /* For sub-worker communication */
    
    /* Resource limits */
    resource_limits_config_t resource_config;
    
    /* Behaviour */
    uint32_t heartbeat_interval_ms;
    uint32_t max_concurrent_tasks;
    int allow_task_cancellation;
    
    /* Storage */
    const char *work_dir;           /* Working directory for temp files */
    const char *result_dir;         /* Directory for results */
} worker_config_t;

/* Worker statistics */
typedef struct {
    uint64_t tasks_executed;
    uint64_t tasks_completed;
    uint64_t tasks_failed;
    uint64_t tasks_cancelled;
    uint64_t total_exec_time_us;
    uint64_t total_queue_time_us;
    resource_stats_t resource_stats;
} worker_stats_t;

/*
 * Create and initialise worker
 *
 * config: worker configuration
 *
 * Returns: worker handle on success, NULL on failure
 */
worker_t *worker_create(const worker_config_t *config);

/*
 * Destroy worker and clean up resources
 */
void worker_destroy(worker_t *worker);

/*
 * Run worker main loop
 *
 * Connects to coordinator, registers, and processes tasks.
 * Blocks until worker_stop() is called or fatal error occurs.
 *
 * Returns: 0 on clean shutdown, negative on error
 */
int worker_run(worker_t *worker);

/*
 * Stop worker gracefully
 *
 * Completes current task (if any) and then shuts down.
 */
void worker_stop(worker_t *worker);

/*
 * Execute a single task
 *
 * Lower-level function for direct task execution.
 * Used internally and by master-worker for sub-task distribution.
 *
 * worker: worker handle
 * task: task to execute
 * result: output buffer for result
 *
 * Returns: 0 on success, negative on error
 */
int worker_execute_task(worker_t *worker,
                        const task_t *task,
                        task_result_t *result);

/*
 * Get worker statistics
 */
void worker_get_stats(worker_t *worker, worker_stats_t *stats);

/*
 * Send heartbeat to coordinator
 *
 * Typically called automatically, but can be invoked manually.
 */
int worker_send_heartbeat(worker_t *worker);

/*
 * Register with coordinator
 *
 * Called automatically during worker_run(), but can be manual.
 *
 * Returns: 0 on success, negative on error
 */
int worker_register(worker_t *worker);

/*
 * Check resource limits and availability
 *
 * Returns: 1 if can accept more tasks, 0 if at capacity
 */
int worker_can_accept_task(worker_t *worker);

#ifdef __cplusplus
}
#endif

#endif /* WORKER_H */