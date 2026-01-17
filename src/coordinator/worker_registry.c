/*
 * Distributed Computing Project - Worker Registry Implementation
 */

#include "worker_registry.h"
#include <stdio.h>
#include <string.h>

const char *worker_state_to_string(worker_state_t state)
{
    switch (state) {
        case WORKER_STATE_UNKNOWN: return "UNKNOWN";
        case WORKER_STATE_IDLE:    return "IDLE";
        case WORKER_STATE_BUSY:    return "BUSY";
        case WORKER_STATE_DEAD:    return "DEAD";
        default:                   return "INVALID";
    }
}

void worker_capabilities_to_string(uint32_t caps, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return;
    
    buf[0] = '\0';
    int first = 1;
    
    if (caps & WORKER_CAP_MPI_CLUSTER) {
        strncat(buf, first ? "MPI" : "|MPI", buf_size - strlen(buf) - 1);
        first = 0;
    }
    if (caps & WORKER_CAP_GPU) {
        strncat(buf, first ? "GPU" : "|GPU", buf_size - strlen(buf) - 1);
        first = 0;
    }
    if (caps & WORKER_CAP_LARGE_MEMORY) {
        strncat(buf, first ? "LARGE_MEM" : "|LARGE_MEM", buf_size - strlen(buf) - 1);
        first = 0;
    }
    if (caps & WORKER_CAP_CHECKPOINT) {
        strncat(buf, first ? "CHECKPOINT" : "|CHECKPOINT", buf_size - strlen(buf) - 1);
        first = 0;
    }
    
    if (first) {
        strncpy(buf, "NONE", buf_size - 1);
    }
}