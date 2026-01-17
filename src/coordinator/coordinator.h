#ifndef COORDINATOR_H
#define COORDINATOR_H

#include <stdint.h>
#include <stddef.h>
#include "../../include/task.h"
#include "../../include/result.h"
#include "../net/transport.h"
#include "../runtime/task_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Coordinator module: coordinator.h
 *
 * Central task coordinator for distributed computing.
 * Manages workers, schedules tasks, and collects results.
 *
 * Supports:
 * - Local workers (MPI)
 * - Remote workers (ZMQ over internet)
 * - Task batching for network efficiency
 * - Worker health monitoring
 * - Result collection and persistence
 */

/* Coordinator handle (opaque) */
typedef struct coordinator coordinator_t;

/* Coordinator configuration */
typedef struct {
    /* Transport configuration */
    transport_type_t transport_type;
    const char *endpoint;           /* For ZMQ */
    int use_mpi;                    /* For MPI */
    
    /* Task management */
    size_t task_queue_size;         /* Maximum pending tasks */
    int allow_task_preemption;      /* Allow cancelling tasks */
    
    /* Worker management */
    uint32_t max_workers;           /* Maximum concurrent workers */
    uint32_t worker_timeout_ms;     /* Heartbeat timeout */
    uint32_t task_timeout_ms;       /* Per-task timeout */
    
    /* Batching (for internet workers) */
    size_t batch_size;              /* Tasks per batch */
    uint32_t batch_timeout_ms;      /* Time to accumulate batch */
    
    /* Persistence */
    const char *result_dir;         /* Directory for results */
    const char *metadata_dir;       /* Directory for metadata */
    int checkpoint_interval_s;      /* Checkpoint frequency */
} coordinator_config_t;

/* Coordinator statistics */
typedef struct {
    uint64_t tasks_submitted;
    uint64_t tasks_completed;
    uint64_t tasks_failed;
    uint64_t tasks_cancelled;
    uint32_t workers_active;
    uint32_t workers_total;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    double avg_task_time_ms;
} coordinator_stats_t;

/*
 * Create and initialise coordinator
 *
 * config: coordinator configuration
 *
 * Returns: coordinator handle on success, NULL on failure
 */
coordinator_t *coordinator_create(const coordinator_config_t *config);

/*
 * Destroy coordinator and clean up resources
 *
 * Sends shutdown messages to all workers and waits briefly.
 */
void coordinator_destroy(coordinator_t *coord);

/*
 * Submit a task for execution
 *
 * coord: coordinator handle
 * task: task to submit
 *
 * Returns: 0 on success, negative on error
 * Errors:
 *   -1: NULL arguments
 *   -2: Queue full
 *   -3: Invalid task
 */
int coordinator_submit_task(coordinator_t *coord, const task_t *task);

/*
 * Submit multiple tasks as a batch
 *
 * More efficient for bulk submissions.
 *
 * coord: coordinator handle
 * tasks: array of tasks
 * count: number of tasks
 * submitted: output - number actually submitted
 *
 * Returns: 0 if all submitted, negative if some failed
 */
int coordinator_submit_batch(coordinator_t *coord,
                             const task_t *tasks,
                             size_t count,
                             size_t *submitted);

/*
 * Run coordinator main loop
 *
 * Handles worker registration, task dispatch, and result collection.
 * Blocks until coordinator_stop() is called or fatal error occurs.
 *
 * Returns: 0 on clean shutdown, negative on error
 */
int coordinator_run(coordinator_t *coord);

/*
 * Stop coordinator gracefully
 *
 * Signals the coordinator to stop accepting new tasks and
 * begin shutdown after pending tasks complete.
 */
void coordinator_stop(coordinator_t *coord);

/*
 * Wait for a specific task to complete
 *
 * coord: coordinator handle
 * task_id: task identifier
 * result: output buffer for result (caller allocated)
 * timeout_ms: timeout in milliseconds (-1 = infinite)
 *
 * Returns: 0 if task completed, -1 on timeout, -2 on task failure
 */
int coordinator_wait_task(coordinator_t *coord,
                          uint64_t task_id,
                          task_result_t *result,
                          int timeout_ms);

/*
 * Wait for all pending tasks to complete
 *
 * Returns: 0 when all tasks done, negative on error
 */
int coordinator_wait_all(coordinator_t *coord, int timeout_ms);

/*
 * Cancel a pending or running task
 *
 * Best effort - may not succeed if task already executing.
 *
 * Returns: 0 if cancelled, -1 if not found or not cancellable
 */
int coordinator_cancel_task(coordinator_t *coord, uint64_t task_id);

/*
 * Get coordinator statistics
 */
void coordinator_get_stats(coordinator_t *coord, coordinator_stats_t *stats);

/*
 * Get number of active workers
 */
uint32_t coordinator_worker_count(coordinator_t *coord);

/*
 * Create checkpoint of coordinator state
 *
 * Saves pending tasks and worker state for recovery.
 *
 * checkpoint_path: path for checkpoint file
 *
 * Returns: 0 on success, negative on error
 */
int coordinator_checkpoint(coordinator_t *coord, const char *checkpoint_path);

/*
 * Restore coordinator from checkpoint
 *
 * checkpoint_path: path to checkpoint file
 *
 * Returns: 0 on success, negative on error
 */
int coordinator_restore(coordinator_t *coord, const char *checkpoint_path);

#ifdef __cplusplus
}
#endif

#endif /* COORDINATOR_H */