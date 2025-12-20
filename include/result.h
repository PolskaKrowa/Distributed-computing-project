#ifndef RESULT_H
#define RESULT_H

#include <stdint.h>
#include <stddef.h>
#include "errcodes.h"

/*
 * Distributed Computing Project - Result definitions
 * 
 * Structures for task results and execution status.
 */

/* Task result structure
 *
 * Returned by workers after task completion.
 * Contains status, timing, and output data location.
 */
typedef struct {
    /* Task identifier (matches task_t.task_id) */
    uint64_t task_id;
    
    /* Execution status code (from errcodes.h) */
    errcode_t status;
    
    /* Output buffer details */
    void *output;
    size_t output_bytes_written;  /* actual bytes written */
    
    /* Timing information (microseconds) */
    uint64_t exec_time_us;
    uint64_t queue_time_us;
    
    /* Worker identification */
    uint32_t worker_id;
    
    /* Optional diagnostic message (null-terminated) */
    char diag_msg[1024];
    
} task_result_t;

/* Status code type alias for clarity */
typedef errcode_t status_t;

/* Helper function to convert status to string */
static inline const char *status_to_string(status_t status)
{
    switch (status) {
        case OK:                      return "OK";
        case ERR_INVALID_ARGUMENT:    return "Invalid argument";
        case ERR_BUFFER_TOO_SMALL:    return "Buffer too small";
        case ERR_COMPUTATION_FAILED:  return "Computation failed";
        case ERR_TIMEOUT:             return "Timeout";
        case ERR_NOT_IMPLEMENTED:     return "Not implemented";
        default:                      return "Unknown error";
    }
}

#endif /* RESULT_H */