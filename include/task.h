#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

/*
 * Distributed Computing Project - Task definitions
 * 
 * Canonical task structure used by coordinators and workers.
 * This is the central data structure for all distributed computation.
 */

/* Task descriptor structure
 *
 * This structure is created by coordinators, transmitted to workers,
 * and used to invoke Fortran computational kernels.
 *
 * Memory ownership:
 * - All buffers (input, output, meta) are owned by C runtime
 * - Fortran code may read/write but must not free
 * - input_size and output_size are in bytes
 */
typedef struct {
    /* Unique identifier for this task across the entire run */
    uint64_t task_id;
    
    /* Model selector - maps to a specific Fortran kernel */
    uint32_t model_id;
    
    /* Input data buffer (read-only for Fortran) */
    const void *input;
    size_t input_size;        /* bytes */
    
    /* Output data buffer (write-only for Fortran) */
    void *output;
    size_t output_size;       /* bytes available */
    
    /* Optional metadata buffer (TLV format) */
    const void *meta;
    size_t meta_size;         /* bytes */
    
    /* Correlation ID for logging and tracing */
    uint64_t trace_id;
    
    /* API version for interface contract validation */
    uint32_t api_version;
    
    /* Optional behaviour flags (see TASK_FLAG_* in project.h) */
    uint32_t flags;
    
    /* Timeout in seconds; <= 0 means no timeout */
    int32_t timeout_secs;
    
} task_t;

/* Task lifecycle states */
typedef enum {
    TASK_STATE_QUEUED = 0,
    TASK_STATE_DISPATCHED = 1,
    TASK_STATE_RUNNING = 2,
    TASK_STATE_COMPLETED = 3,
    TASK_STATE_FAILED = 4,
    TASK_STATE_TIMEOUT = 5,
    TASK_STATE_CANCELLED = 6,
} task_state_t;

#endif /* TASK_H */