/*
 * Distributed Computing Project - Executor Implementation
 */

#include "executor.h"
#include "../common/log.h"
#include "../../include/fortran_api.h"
#include "../../include/errcodes.h"

#include <string.h>
#include <time.h>

int executor_execute_task(const task_t *task, task_result_t *result)
{
    if (!task || !result) {
        log_error("executor_execute_task: NULL argument");
        return -1;
    }
    
    /* Initialize result */
    memset(result, 0, sizeof(*result));
    result->task_id = task->task_id;
    
    /* Validate task */
    if (!task->input || task->input_size == 0) {
        log_error("Task %lu has no input data", task->task_id);
        result->status = ERR_INVALID_ARGUMENT;
        return -2;
    }
    
    if (!task->output || task->output_size == 0) {
        log_error("Task %lu has no output buffer", task->task_id);
        result->status = ERR_INVALID_ARGUMENT;
        return -3;
    }
    
    /* Record start time */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    log_debug("Executing task %lu (model=%u, input=%zu bytes, output=%zu bytes)",
             task->task_id, task->model_id, task->input_size, task->output_size);
    
    /* Call Fortran computational kernel */
    int32_t fortran_status;
    int32_t rc = fortran_model_run_v1(
        task->input,
        task->input_size,
        task->output,
        task->output_size,
        task->meta,
        task->meta_size,
        task->trace_id,
        &fortran_status
    );
    
    /* Record end time */
    clock_gettime(CLOCK_MONOTONIC, &end);
    result->exec_time_us = (end.tv_sec - start.tv_sec) * 1000000UL +
                          (end.tv_nsec - start.tv_nsec) / 1000UL;
    
    /* Fill result */
    result->status = rc;
    result->output = task->output;
    result->output_bytes_written = task->output_size; /* Assume full write for now */
    
    if (rc == OK) {
        log_info("Task %lu completed successfully (exec_time=%lu us)",
                task->task_id, result->exec_time_us);
    } else {
        log_error("Task %lu failed with status %d (exec_time=%lu us)",
                 task->task_id, rc, result->exec_time_us);
        snprintf(result->diag_msg, sizeof(result->diag_msg),
                "Fortran kernel returned error code %d", rc);
    }
    
    return 0;
}

int executor_execute_batch(const task_t *tasks, size_t count,
                           task_result_t *results)
{
    if (!tasks || !results || count == 0) {
        log_error("executor_execute_batch: invalid arguments");
        return 0;
    }
    
    log_info("Executing batch of %zu tasks", count);
    
    int success_count = 0;
    
    /* Execute tasks sequentially */
    /* In a more advanced implementation, could use thread pool */
    for (size_t i = 0; i < count; i++) {
        if (executor_execute_task(&tasks[i], &results[i]) == 0 &&
            results[i].status == OK) {
            success_count++;
        }
    }
    
    log_info("Batch execution complete: %d/%zu tasks successful",
             success_count, count);
    
    return success_count;
}