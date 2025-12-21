#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transport types available */
typedef enum {
    TRANSPORT_TYPE_MPI = 0,
    TRANSPORT_TYPE_ZMQ = 1
} transport_type_t;

/* Message types for distributed communication */
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

/* Message envelope header (fixed size) */
typedef struct {
    uint32_t magic;         /* MESSAGE_MAGIC from project.h */
    uint16_t version;       /* Envelope version */
    uint16_t msg_type;      /* From message_type_t */
    uint64_t task_id;       /* Task identifier (0 if N/A) */
    uint32_t payload_len;   /* Length of payload in bytes */
} __attribute__((packed)) message_header_t;

/* Complete message structure */
typedef struct {
    message_header_t header;
    void *payload;          /* Payload data (owned by message) */
    size_t payload_capacity; /* Allocated capacity */
} message_t;

/* Transport handle (opaque from user perspective)
 * Backend implementations use the vtable to dispatch operations */
typedef struct transport {
    transport_type_t type;
    void *vtable;      /* Function pointer table - backend specific */
    void *impl;        /* Backend-specific implementation data */
} transport_t;

/* Connection role */
typedef enum {
    TRANSPORT_ROLE_COORDINATOR,
    TRANSPORT_ROLE_WORKER
} transport_role_t;

/* Transport configuration */
typedef struct {
    transport_type_t type;
    transport_role_t role;
    const char *endpoint;   /* Address/URL (ZMQ) or NULL (MPI) */
    int timeout_ms;         /* Timeout for operations (-1 = infinite) */
    int max_workers;        /* Expected worker count (coordinator only) */
} transport_config_t;

/* Transport statistics */
typedef struct {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t errors;
} transport_stats_t;

/*
 * Transport lifecycle
 */

/* Initialize transport layer
 * Returns: 0 on success, negative on error */
int transport_init(transport_t **out, const transport_config_t *config);

/* Shutdown and free transport
 * Returns: 0 on success */
int transport_shutdown(transport_t *t);

/*
 * Message operations
 */

/* Allocate a new message with given payload capacity
 * Returns: message pointer or NULL on error */
message_t *message_alloc(size_t payload_capacity);

/* Free a message and its payload */
void message_free(message_t *msg);

/* Set message header fields */
void message_set_header(message_t *msg, uint16_t msg_type, uint64_t task_id);

/* Validate message header
 * Returns: 0 if valid, negative on error */
int message_validate(const message_t *msg);

/*
 * Send/Receive operations
 */

/* Send a message
 * dst_rank: destination rank (MPI) or ignored (ZMQ)
 * Returns: 0 on success, negative on error */
int transport_send(transport_t *t, const message_t *msg, int dst_rank);

/* Receive a message (blocking with timeout)
 * src_rank: output parameter for source rank (MPI only, may be NULL)
 * Returns: received message or NULL on error/timeout */
message_t *transport_recv(transport_t *t, int *src_rank);

/* Check if a message is available without blocking
 * Returns: 1 if available, 0 if not, negative on error */
int transport_poll(transport_t *t, int timeout_ms);

/*
 * Broadcast operations (coordinator only)
 */

/* Broadcast message to all workers
 * Returns: 0 on success, negative on error */
int transport_broadcast(transport_t *t, const message_t *msg);

/*
 * Query functions
 */

/* Get transport statistics */
void transport_get_stats(const transport_t *t, transport_stats_t *stats);

/* Get number of active workers (coordinator only)
 * Returns: worker count or negative on error */
int transport_worker_count(const transport_t *t);

/* Get local rank/ID
 * Returns: rank (MPI) or worker ID (ZMQ), or negative on error */
int transport_get_rank(const transport_t *t);

/*
 * Utility functions
 */

/* Convert message type to string (for logging) */
const char *message_type_to_string(message_type_t type);

/* Get transport type name */
const char *transport_type_to_string(transport_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_H */