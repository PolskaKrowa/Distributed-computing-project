#include "transport.h"
#include "../common/log.h"

#include <stdlib.h>
#include <string.h>
#include <mpi.h>

#define MESSAGE_MAGIC 0xDA7A1E00
#define ENVELOPE_VERSION 1

/* MPI tags for different message types */
#define MPI_TAG_HEADER  1
#define MPI_TAG_PAYLOAD 2

/* Transport implementation for MPI */
struct transport {
    transport_type_t type;
    transport_role_t role;
    int rank;
    int size;
    int timeout_ms;
    transport_stats_t stats;
    MPI_Comm comm;
};

/*
 * Helper: probe for incoming messages with timeout
 * Returns: 1 if message available, 0 on timeout, negative on error
 */
static int mpi_probe_with_timeout(MPI_Comm comm, int source, int tag, 
                                   int timeout_ms, MPI_Status *status)
{
    if (timeout_ms < 0) {
        /* Blocking probe */
        int rc = MPI_Probe(source, tag, comm, status);
        return (rc == MPI_SUCCESS) ? 1 : -1;
    }

    /* Poll with timeout */
    const int poll_interval_us = 1000; /* 1ms */
    int elapsed_us = 0;
    int flag;

    while (elapsed_us < timeout_ms * 1000) {
        int rc = MPI_Iprobe(source, tag, comm, &flag, status);
        if (rc != MPI_SUCCESS) return -1;
        if (flag) return 1;

        usleep(poll_interval_us);
        elapsed_us += poll_interval_us;
    }

    return 0; /* timeout */
}

int transport_init_mpi(transport_t **out, const transport_config_t *config)
{
    if (!out || !config) return -1;

    /* Initialize MPI if not already done */
    int initialized;
    MPI_Initialized(&initialized);
    if (!initialized) {
        int rc = MPI_Init(NULL, NULL);
        if (rc != MPI_SUCCESS) {
            log_error("MPI_Init failed");
            return -3;
        }
    }

    transport_t *t = calloc(1, sizeof(*t));
    if (!t) return -4;

    t->type = TRANSPORT_TYPE_MPI;
    t->role = config->role;
    t->timeout_ms = config->timeout_ms;
    t->comm = MPI_COMM_WORLD;

    MPI_Comm_rank(t->comm, &t->rank);
    MPI_Comm_size(t->comm, &t->size);

    log_info("MPI transport initialized (rank=%d, size=%d, role=%s)",
             t->rank, t->size,
             t->role == TRANSPORT_ROLE_COORDINATOR ? "coordinator" : "worker");

    *out = t;
    return 0;
}

int transport_shutdown(transport_t *t)
{
    if (!t) return -1;

    log_info("MPI transport shutting down (rank=%d, sent=%lu, recv=%lu)",
             t->rank, t->stats.messages_sent, t->stats.messages_received);

    free(t);

    /* Don't finalize MPI here - caller may need to do cleanup first */
    return 0;
}

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
    msg->header.magic = MESSAGE_MAGIC;
    msg->header.version = ENVELOPE_VERSION;

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
    if (msg->header.magic != MESSAGE_MAGIC) {
        log_error("Invalid message magic: 0x%08x (expected 0x%08x)",
                  msg->header.magic, MESSAGE_MAGIC);
        return -2;
    }
    if (msg->header.version != ENVELOPE_VERSION) {
        log_warn("Message version mismatch: %d (expected %d)",
                 msg->header.version, ENVELOPE_VERSION);
        return -3;
    }
    if (msg->header.payload_len > msg->payload_capacity) {
        log_error("Payload length (%u) exceeds capacity (%zu)",
                  msg->header.payload_len, msg->payload_capacity);
        return -4;
    }
    return 0;
}

int transport_send(transport_t *t, const message_t *msg, int dst_rank)
{
    if (!t || !msg) return -1;
    if (message_validate(msg) != 0) return -2;

    /* Send header */
    int rc = MPI_Send(&msg->header, sizeof(message_header_t), MPI_BYTE,
                      dst_rank, MPI_TAG_HEADER, t->comm);
    if (rc != MPI_SUCCESS) {
        log_error("MPI_Send header failed to rank %d", dst_rank);
        t->stats.errors++;
        return -3;
    }

    /* Send payload if present */
    if (msg->header.payload_len > 0 && msg->payload) {
        rc = MPI_Send(msg->payload, msg->header.payload_len, MPI_BYTE,
                      dst_rank, MPI_TAG_PAYLOAD, t->comm);
        if (rc != MPI_SUCCESS) {
            log_error("MPI_Send payload failed to rank %d", dst_rank);
            t->stats.errors++;
            return -4;
        }
    }

    t->stats.messages_sent++;
    t->stats.bytes_sent += sizeof(message_header_t) + msg->header.payload_len;

    log_debug("MPI sent message type=%s to rank=%d (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type), dst_rank,
              msg->header.task_id, msg->header.payload_len);

    return 0;
}

message_t *transport_recv(transport_t *t, int *src_rank)
{
    if (!t) return NULL;

    MPI_Status status;

    /* Probe for header */
    int probe_rc = mpi_probe_with_timeout(t->comm, MPI_ANY_SOURCE, 
                                          MPI_TAG_HEADER, t->timeout_ms, &status);
    if (probe_rc <= 0) {
        if (probe_rc < 0) {
            log_error("MPI_Probe failed");
            t->stats.errors++;
        }
        return NULL; /* timeout or error */
    }

    int source = status.MPI_SOURCE;

    /* Receive header */
    message_header_t header;
    int rc = MPI_Recv(&header, sizeof(header), MPI_BYTE,
                      source, MPI_TAG_HEADER, t->comm, &status);
    if (rc != MPI_SUCCESS) {
        log_error("MPI_Recv header failed from rank %d", source);
        t->stats.errors++;
        return NULL;
    }

    /* Allocate message */
    message_t *msg = message_alloc(header.payload_len);
    if (!msg) {
        log_error("Failed to allocate message (payload=%u bytes)", header.payload_len);
        return NULL;
    }

    memcpy(&msg->header, &header, sizeof(header));

    /* Receive payload if present */
    if (header.payload_len > 0) {
        rc = MPI_Recv(msg->payload, header.payload_len, MPI_BYTE,
                      source, MPI_TAG_PAYLOAD, t->comm, &status);
        if (rc != MPI_SUCCESS) {
            log_error("MPI_Recv payload failed from rank %d", source);
            message_free(msg);
            t->stats.errors++;
            return NULL;
        }
    }

    /* Validate message */
    if (message_validate(msg) != 0) {
        log_error("Received invalid message from rank %d", source);
        message_free(msg);
        return NULL;
    }

    if (src_rank) *src_rank = source;

    t->stats.messages_received++;
    t->stats.bytes_received += sizeof(message_header_t) + header.payload_len;

    log_debug("MPI received message type=%s from rank=%d (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type), source,
              msg->header.task_id, msg->header.payload_len);

    return msg;
}

int transport_poll(transport_t *t, int timeout_ms)
{
    if (!t) return -1;

    MPI_Status status;
    return mpi_probe_with_timeout(t->comm, MPI_ANY_SOURCE, 
                                   MPI_TAG_HEADER, timeout_ms, &status);
}

int transport_broadcast(transport_t *t, const message_t *msg)
{
    if (!t || !msg) return -1;
    if (t->role != TRANSPORT_ROLE_COORDINATOR) {
        log_error("Only coordinator can broadcast");
        return -2;
    }

    /* In MPI, broadcast to all workers (ranks 1 through size-1) */
    int errors = 0;
    for (int i = 1; i < t->size; i++) {
        if (transport_send(t, msg, i) != 0) {
            errors++;
        }
    }

    return errors > 0 ? -3 : 0;
}

void transport_get_stats(const transport_t *t, transport_stats_t *stats)
{
    if (!t || !stats) return;
    memcpy(stats, &t->stats, sizeof(*stats));
}

int transport_worker_count(const transport_t *t)
{
    if (!t) return -1;
    if (t->role != TRANSPORT_ROLE_COORDINATOR) return -2;
    
    /* In MPI, all ranks except 0 (coordinator) are workers */
    return t->size - 1;
}

int transport_get_rank(const transport_t *t)
{
    return t ? t->rank : -1;
}

const char *message_type_to_string(message_type_t type)
{
    switch (type) {
        case MSG_TYPE_HEARTBEAT:       return "HEARTBEAT";
        case MSG_TYPE_TASK_SUBMIT:     return "TASK_SUBMIT";
        case MSG_TYPE_TASK_RESULT:     return "TASK_RESULT";
        case MSG_TYPE_TASK_CANCEL:     return "TASK_CANCEL";
        case MSG_TYPE_WORKER_REGISTER: return "WORKER_REGISTER";
        case MSG_TYPE_WORKER_SHUTDOWN: return "WORKER_SHUTDOWN";
        case MSG_TYPE_COORDINATOR_CMD: return "COORDINATOR_CMD";
        case MSG_TYPE_ERROR:           return "ERROR";
        default:                       return "UNKNOWN";
    }
}

const char *transport_type_to_string(transport_type_t type)
{
    switch (type) {
        case TRANSPORT_TYPE_MPI: return "MPI";
        case TRANSPORT_TYPE_ZMQ: return "ZeroMQ";
        default:                 return "UNKNOWN";
    }
}