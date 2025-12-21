/*
 * Transport layer dispatcher
 * 
 * Routes transport_init() calls to the appropriate implementation
 * (MPI or ZeroMQ) based on the configuration.
 * Uses function pointers (vtable) to dispatch operations to backend implementations.
 */

#include "transport.h"
#include "../common/log.h"

#include <stdlib.h>
#include <string.h>

/* Function pointer typedef for backend operations */
typedef struct {
    int (*shutdown)(struct transport *t);
    int (*send)(struct transport *t, const message_t *msg, int dst_rank);
    message_t* (*recv)(struct transport *t, int *src_rank);
    int (*poll)(struct transport *t, int timeout_ms);
    int (*broadcast)(struct transport *t, const message_t *msg);
    void (*get_stats)(const struct transport *t, transport_stats_t *stats);
    int (*worker_count)(const struct transport *t);
    int (*get_rank)(const struct transport *t);
} transport_vtable_t;

/* Transport implementation structure */
struct transport {
    transport_type_t type;
    transport_vtable_t *vtable;  /* Function pointers */
    void *impl;                   /* Backend-specific data */
};

/* Forward declarations of implementation-specific init functions */
#ifdef HAVE_MPI
extern int transport_init_mpi(transport_t **out, const transport_config_t *config);
#endif

#ifdef HAVE_ZMQ
extern int transport_init_zmq(transport_t **out, const transport_config_t *config);
#endif

/*
 * Dispatcher for transport initialization
 */
int transport_init(transport_t **out, const transport_config_t *config)
{
    if (!out || !config) {
        log_error("transport_init: NULL argument");
        return -1;
    }

    switch (config->type) {
        case TRANSPORT_TYPE_MPI:
#ifdef HAVE_MPI
            return transport_init_mpi(out, config);
#else
            log_error("transport_init: MPI backend not available (compile with -DHAVE_MPI)");
            return -3;
#endif
        case TRANSPORT_TYPE_ZMQ:
#ifdef HAVE_ZMQ
            return transport_init_zmq(out, config);
#else
            log_error("transport_init: ZMQ backend not available (compile with -DHAVE_ZMQ)");
            return -4;
#endif
        default:
            log_error("transport_init: unknown transport type %d", config->type);
            return -2;
    }
}

/*
 * Public dispatch functions - delegate to backend via vtable
 */

int transport_shutdown(transport_t *t)
{
    if (!t || !t->vtable || !t->vtable->shutdown) {
        log_error("transport_shutdown: invalid transport");
        return -1;
    }
    return t->vtable->shutdown(t);
}

int transport_send(transport_t *t, const message_t *msg, int dst_rank)
{
    if (!t || !t->vtable || !t->vtable->send) {
        log_error("transport_send: invalid transport");
        return -1;
    }
    return t->vtable->send(t, msg, dst_rank);
}

message_t *transport_recv(transport_t *t, int *src_rank)
{
    if (!t || !t->vtable || !t->vtable->recv) {
        log_error("transport_recv: invalid transport");
        return NULL;
    }
    return t->vtable->recv(t, src_rank);
}

int transport_poll(transport_t *t, int timeout_ms)
{
    if (!t || !t->vtable || !t->vtable->poll) {
        log_error("transport_poll: invalid transport");
        return -1;
    }
    return t->vtable->poll(t, timeout_ms);
}

int transport_broadcast(transport_t *t, const message_t *msg)
{
    if (!t || !t->vtable || !t->vtable->broadcast) {
        log_error("transport_broadcast: invalid transport");
        return -1;
    }
    return t->vtable->broadcast(t, msg);
}

void transport_get_stats(const transport_t *t, transport_stats_t *stats)
{
    if (!t || !t->vtable || !t->vtable->get_stats) {
        log_error("transport_get_stats: invalid transport");
        return;
    }
    t->vtable->get_stats(t, stats);
}

int transport_worker_count(const transport_t *t)
{
    if (!t || !t->vtable || !t->vtable->worker_count) {
        log_error("transport_worker_count: invalid transport");
        return -1;
    }
    return t->vtable->worker_count(t);
}

int transport_get_rank(const transport_t *t)
{
    if (!t || !t->vtable || !t->vtable->get_rank) {
        log_error("transport_get_rank: invalid transport");
        return -1;
    }
    return t->vtable->get_rank(t);
}

/*
 * Message utility functions - backend agnostic
 */

message_t *message_alloc(size_t payload_capacity)
{
    message_t *msg = calloc(1, sizeof(*msg));
    if (!msg) return NULL;

    if (payload_capacity > 0) {
        msg->payload = malloc(payload_capacity);
        if (!msg->payload) {
            free(msg);
            return NULL;
        }
    }

    msg->payload_capacity = payload_capacity;
    msg->header.magic = 0xDA7A1E00; /* MESSAGE_MAGIC */
    msg->header.version = 1;        /* ENVELOPE_VERSION */

    return msg;
}

void message_free(message_t *msg)
{
    if (!msg) return;
    free(msg->payload);
    free(msg);
}

void message_set_header(message_t *msg, uint16_t msg_type, uint64_t task_id)
{
    if (!msg) return;
    msg->header.msg_type = msg_type;
    msg->header.task_id = task_id;
}

int message_validate(const message_t *msg)
{
    if (!msg) return -1;
    if (msg->header.magic != 0xDA7A1E00) { /* MESSAGE_MAGIC */
        log_error("Invalid message magic: 0x%08x (expected 0x%08x)",
                  msg->header.magic, 0xDA7A1E00);
        return -2;
    }
    if (msg->header.version != 1) { /* ENVELOPE_VERSION */
        log_warn("Message version mismatch: %d (expected %d)",
                 msg->header.version, 1);
        return -3;
    }
    if (msg->header.payload_len > msg->payload_capacity) {
        log_error("Payload length (%u) exceeds capacity (%zu)",
                  msg->header.payload_len, msg->payload_capacity);
        return -4;
    }
    return 0;
}

/*
 * Convert transport type to string
 */
const char *transport_type_to_string(transport_type_t type)
{
    switch (type) {
        case TRANSPORT_TYPE_MPI:
            return "MPI";
        case TRANSPORT_TYPE_ZMQ:
            return "ZeroMQ";
        default:
            return "Unknown";
    }
}

/*
 * Convert message type to string
 */
const char *message_type_to_string(message_type_t type)
{
    switch (type) {
        case MSG_TYPE_HEARTBEAT:
            return "HEARTBEAT";
        case MSG_TYPE_TASK_SUBMIT:
            return "TASK_SUBMIT";
        case MSG_TYPE_TASK_RESULT:
            return "TASK_RESULT";
        case MSG_TYPE_TASK_CANCEL:
            return "TASK_CANCEL";
        case MSG_TYPE_WORKER_REGISTER:
            return "WORKER_REGISTER";
        case MSG_TYPE_WORKER_SHUTDOWN:
            return "WORKER_SHUTDOWN";
        case MSG_TYPE_COORDINATOR_CMD:
            return "COORDINATOR_CMD";
        case MSG_TYPE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}
