#ifndef RUNTIME_TASK_QUEUE_H
#define RUNTIME_TASK_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include "task.h"

/** Opaque task queue type */
typedef struct task_queue task_queue_t;

/**
 * Create a task queue with an initial capacity. The queue is thread-safe.
 * Returns NULL on allocation failure.
 */
task_queue_t *task_queue_create(size_t initial_capacity);

/** Destroy a queue and free resources. The queue must not be in use by other threads.
 * Note: this does not free tasks still held in the queue; caller must drain or free them.
 */
void task_queue_destroy(task_queue_t *q);

/** Push a task into the queue. This call blocks if the queue is full until space is available.
 * Returns 0 on success, -1 on error.
 */
int task_queue_push(task_queue_t *q, task_t *task);

/** Try to pop a task from the queue without blocking. Returns the task or NULL if empty. */
task_t *task_queue_try_pop(task_queue_t *q);

/** Pop a task from the queue, blocking until one is available. Returns the task (owned by caller). */
task_t *task_queue_pop(task_queue_t *q);

/** Peek at the front task without removing it. Returns NULL if empty. */
task_t *task_queue_peek(task_queue_t *q);

/** Return current number of elements in the queue. */
size_t task_queue_size(const task_queue_t *q);

/** Returns true if the queue is empty. */
bool task_queue_is_empty(const task_queue_t *q);

#endif // RUNTIME_TASK_QUEUE_H