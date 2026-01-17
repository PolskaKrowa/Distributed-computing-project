#ifndef PROJECT_H
#define PROJECT_H

#include <stdint.h>

/*
 * Distributed Computing Project - Main project header
 * 
 * Contains project-wide definitions, version information, and
 * common constants used across the entire codebase.
 */

/* Project version - semantic versioning */
#define PROJECT_VERSION_MAJOR 0
#define PROJECT_VERSION_MINOR 1
#define PROJECT_VERSION_PATCH 0

/* API version for Fortran/C boundary */
#define API_VERSION_CURRENT 1

/* Magic numbers for data validation */
#define MESSAGE_MAGIC 0xDA7A1E00u
#define CHECKPOINT_MAGIC 0xC8EC7001u

/* Resource limits and buffer sizes */
#define MAX_WORKERS 1024
#define MAX_TASK_QUEUE_SIZE 65536
#define MAX_MODEL_NAME_LEN 256
#define MAX_DIAG_MSG_LEN 1024

/* TLV metadata type codes */
#define TLV_TYPE_SEED 0x0001
#define TLV_TYPE_PRECISION 0x0002
#define TLV_TYPE_SHAPE 0x0003
#define TLV_TYPE_HYPERPARAM 0x0004

/* Task flags (bitfield) */
#define TASK_FLAG_CHECKPOINT_ENABLED (1u << 0)
#define TASK_FLAG_HIGH_PRIORITY (1u << 1)
#define TASK_FLAG_VERBOSE_LOGGING (1u << 2)

/* Message types for transport layer */
typedef enum {
    MSG_TYPE_HEARTBEAT       = 0x0001,  /* Worker heartbeat */
    MSG_TYPE_TASK_SUBMIT     = 0x0010,  /* Coordinator -> Worker: task */
    MSG_TYPE_TASK_RESULT     = 0x0011,  /* Worker -> Coordinator: result */
    MSG_TYPE_TASK_CANCEL     = 0x0012,  /* Coordinator -> Worker: cancel */
    MSG_TYPE_WORKER_REGISTER = 0x0020,  /* Worker -> Coordinator: join */
    MSG_TYPE_WORKER_SHUTDOWN = 0x0021,  /* Worker -> Coordinator: leave */
    MSG_TYPE_COORDINATOR_CMD = 0x0030,  /* Coordinator control commands */
    MSG_TYPE_ERROR           = 0x00FF   /* Error message */
} message_type_t;

#endif /* PROJECT_H */