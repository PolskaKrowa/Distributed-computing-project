#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include "../../include/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime module: task_queue.h
 *
 * Thread-safe task queue for coordinators and workers.
 * Supports priority queuing, blocking operations, and statistics.
 *
 * Design:
 * - FIFO ordering within priority levels
 * - High priority tasks jump ahead of normal tasks
 * - Thread-safe with mutexes and condition variables
 * - Configurable capacity limits
 * - Non-blocking poll and blocking wait operations
 */

/* Task queue handle (opaque) */
typedef struct task_queue task_queue_t;

/* Task queue statistics */
typedef struct {
    size_t current_size;        /* Current number of tasks */
    size_t max_size;            /* Maximum capacity */
    uint64_t total_enqueued;    /* Total tasks ever added */
    uint64_t total_dequeued;    /* Total tasks ever removed */
    uint64_t high_priority_count; /* Current high priority tasks */
    uint64_t blocked_threads;   /* Threads waiting on queue */
} task_queue_stats_t;

/* Task queue configuration */
typedef struct {
    size_t max_size;            /* Maximum queue size (0 = unlimited) */
    int allow_duplicates;       /* Allow duplicate task IDs */
} task_queue_config_t;

/*
 * Create a new task queue
 *
 * config: queue configuration (NULL = use defaults)
 *
 * Returns: queue handle on success, NULL on failure
 */
task_queue_t *task_queue_create(const task_queue_config_t *config);

/*
 * Destroy task queue and free resources
 *
 * Any remaining tasks are freed.
 * Blocked threads will receive NULL from dequeue.
 */
void task_queue_destroy(task_queue_t *queue);

/*
 * Add task to queue
 *
 * queue: queue handle
 * task: task to enqueue (copied internally)
 *
 * Returns: 0 on success, negative on error
 * Errors:
 *   -1: NULL arguments
 *   -2: Queue full
 *   -3: Duplicate task ID (if duplicates not allowed)
 */
int task_queue_enqueue(task_queue_t *queue, const task_t *task);

/*
 * Remove task from queue (non-blocking)
 *
 * queue: queue handle
 * task: output buffer for task (caller allocated)
 *
 * Returns: 0 on success, -1 if queue empty
 */
int task_queue_dequeue(task_queue_t *queue, task_t *task);

/*
 * Remove task from queue with timeout (blocking)
 *
 * Blocks until a task is available or timeout expires.
 *
 * queue: queue handle
 * task: output buffer for task
 * timeout_ms: timeout in milliseconds (-1 = infinite)
 *
 * Returns:
 *   0: task dequeued successfully
 *  -1: timeout expired
 *  -2: queue destroyed while waiting
 */
int task_queue_dequeue_wait(task_queue_t *queue, task_t *task, int timeout_ms);

/*
 * Peek at next task without removing it
 *
 * Returns: 0 on success, -1 if queue empty
 */
int task_queue_peek(task_queue_t *queue, task_t *task);

/*
 * Remove specific task by ID
 *
 * Useful for cancellation.
 *
 * Returns: 0 if found and removed, -1 if not found
 */
int task_queue_remove_by_id(task_queue_t *queue, uint64_t task_id);

/*
 * Check if queue is empty
 */
int task_queue_is_empty(const task_queue_t *queue);

/*
 * Check if queue is full
 */
int task_queue_is_full(const task_queue_t *queue);

/*
 * Get current queue size
 */
size_t task_queue_size(const task_queue_t *queue);

/*
 * Get queue statistics
 */
void task_queue_get_stats(const task_queue_t *queue, task_queue_stats_t *stats);

/*
 * Clear all tasks from queue
 *
 * Returns: number of tasks removed
 */
size_t task_queue_clear(task_queue_t *queue);

/*
 * Wake all threads waiting on queue
 *
 * Threads will return with error code.
 * Used during shutdown.
 */
void task_queue_wake_all(task_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* TASK_QUEUE_H */