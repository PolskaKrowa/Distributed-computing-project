/*
 * Distributed Computing Project - Worker Registry
 * 
 * Tracks registered workers and their capabilities.
 */

#ifndef WORKER_REGISTRY_H
#define WORKER_REGISTRY_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Worker states */
typedef enum {
    WORKER_STATE_UNKNOWN = 0,
    WORKER_STATE_IDLE = 1,
    WORKER_STATE_BUSY = 2,
    WORKER_STATE_DEAD = 3
} worker_state_t;

/* Worker capability flags */
#define WORKER_CAP_MPI_CLUSTER    (1u << 0)  /* Has local MPI cluster */
#define WORKER_CAP_GPU            (1u << 1)  /* Has GPU available */
#define WORKER_CAP_LARGE_MEMORY   (1u << 2)  /* Has >64GB RAM */
#define WORKER_CAP_CHECKPOINT     (1u << 3)  /* Supports checkpointing */

/* Worker information structure */
typedef struct {
    uint32_t worker_id;
    worker_state_t state;
    time_t last_heartbeat;
    time_t registered;
    
    /* Capabilities */
    uint32_t num_local_ranks;    /* MPI ranks in local cluster (1 = standalone) */
    uint32_t max_batch_size;     /* Maximum tasks per batch */
    uint32_t capabilities;       /* Capability flags */
    
    /* Current load */
    uint32_t tasks_running;
    uint32_t tasks_completed;
    uint32_t tasks_failed;
    
    /* Performance metrics */
    uint64_t total_cpu_time_us;
    uint64_t total_queue_time_us;
    double avg_task_time_ms;
} worker_info_t;

/*
 * Get human-readable string for worker state
 */
const char *worker_state_to_string(worker_state_t state);

/*
 * Get human-readable string for capabilities
 */
void worker_capabilities_to_string(uint32_t caps, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* WORKER_REGISTRY_H */