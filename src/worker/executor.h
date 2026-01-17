/*
 * Distributed Computing Project - Executor
 * 
 * Task execution engine for workers.
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "../../include/task.h"
#include "../../include/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Execute a single task
 * 
 * Calls the appropriate Fortran kernel based on task.model_id
 * and fills in the result structure.
 * 
 * task: task to execute
 * result: output result structure (caller allocated)
 * 
 * Returns: 0 on success, negative on error
 */
int executor_execute_task(const task_t *task, task_result_t *result);

/*
 * Execute a batch of tasks
 * 
 * Executes tasks sequentially or in parallel depending on
 * available resources.
 * 
 * tasks: array of tasks
 * count: number of tasks
 * results: output array of results (caller allocated)
 * 
 * Returns: number of successfully executed tasks
 */
int executor_execute_batch(const task_t *tasks, size_t count,
                           task_result_t *results);

#ifdef __cplusplus
}
#endif

#endif /* EXECUTOR_H */