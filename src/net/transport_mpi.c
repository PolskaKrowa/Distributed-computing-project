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

/* MPI-specific transport implementation structure */
struct mpi_transport {
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

/* Backend-specific implementations (static) */

static int mpi_shutdown(struct transport *t)
{
    if (!t || !t->impl) return -1;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;

    log_info("MPI transport shutting down (rank=%d, sent=%lu, recv=%lu)",
             mpi->rank, mpi->stats.messages_sent, mpi->stats.messages_received);

    free(mpi);
    free(t->vtable);
    free(t);

    /* Don't finalize MPI here - caller may need to do cleanup first */
    return 0;
}

static int mpi_send(struct transport *t, const message_t *msg, int dst_rank)
{
    if (!t || !t->impl || !msg) return -1;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;
    
    if (message_validate(msg) != 0) return -2;

    /* Send header */
    int rc = MPI_Send(&msg->header, sizeof(message_header_t), MPI_BYTE,
                      dst_rank, MPI_TAG_HEADER, mpi->comm);
    if (rc != MPI_SUCCESS) {
        log_error("MPI_Send header failed to rank %d", dst_rank);
        mpi->stats.errors++;
        return -3;
    }

    /* Send payload if present */
    if (msg->header.payload_len > 0 && msg->payload) {
        rc = MPI_Send(msg->payload, msg->header.payload_len, MPI_BYTE,
                      dst_rank, MPI_TAG_PAYLOAD, mpi->comm);
        if (rc != MPI_SUCCESS) {
            log_error("MPI_Send payload failed to rank %d", dst_rank);
            mpi->stats.errors++;
            return -4;
        }
    }

    mpi->stats.messages_sent++;
    mpi->stats.bytes_sent += sizeof(message_header_t) + msg->header.payload_len;

    log_debug("MPI sent message type=%s to rank=%d (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type), dst_rank,
              msg->header.task_id, msg->header.payload_len);

    return 0;
}

static message_t *mpi_recv(struct transport *t, int *src_rank)
{
    if (!t || !t->impl) return NULL;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;

    MPI_Status status;

    /* Probe for header */
    int probe_rc = mpi_probe_with_timeout(mpi->comm, MPI_ANY_SOURCE, 
                                          MPI_TAG_HEADER, mpi->timeout_ms, &status);
    if (probe_rc <= 0) {
        if (probe_rc < 0) {
            log_error("MPI_Probe failed");
            mpi->stats.errors++;
        }
        return NULL; /* timeout or error */
    }

    int source = status.MPI_SOURCE;

    /* Receive header */
    message_header_t header;
    int rc = MPI_Recv(&header, sizeof(header), MPI_BYTE,
                      source, MPI_TAG_HEADER, mpi->comm, &status);
    if (rc != MPI_SUCCESS) {
        log_error("MPI_Recv header failed from rank %d", source);
        mpi->stats.errors++;
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
                      source, MPI_TAG_PAYLOAD, mpi->comm, &status);
        if (rc != MPI_SUCCESS) {
            log_error("MPI_Recv payload failed from rank %d", source);
            message_free(msg);
            mpi->stats.errors++;
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

    mpi->stats.messages_received++;
    mpi->stats.bytes_received += sizeof(message_header_t) + header.payload_len;

    log_debug("MPI received message type=%s from rank=%d (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type), source,
              msg->header.task_id, msg->header.payload_len);

    return msg;
}

static int mpi_poll(struct transport *t, int timeout_ms)
{
    if (!t || !t->impl) return -1;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;

    MPI_Status status;
    return mpi_probe_with_timeout(mpi->comm, MPI_ANY_SOURCE, 
                                   MPI_TAG_HEADER, timeout_ms, &status);
}

static int mpi_broadcast(struct transport *t, const message_t *msg)
{
    if (!t || !t->impl || !msg) return -1;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;
    
    if (mpi->role != TRANSPORT_ROLE_COORDINATOR) {
        log_error("Only coordinator can broadcast");
        return -2;
    }

    /* In MPI, broadcast to all workers (ranks 1 through size-1) */
    int errors = 0;
    for (int i = 1; i < mpi->size; i++) {
        if (mpi_send(t, msg, i) != 0) {
            errors++;
        }
    }

    return errors > 0 ? -3 : 0;
}

static void mpi_get_stats(const struct transport *t, transport_stats_t *stats)
{
    if (!t || !t->impl || !stats) return;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;
    memcpy(stats, &mpi->stats, sizeof(*stats));
}

static int mpi_worker_count(const struct transport *t)
{
    if (!t || !t->impl) return -1;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;
    
    if (mpi->role != TRANSPORT_ROLE_COORDINATOR) return -2;
    
    /* In MPI, all ranks except 0 (coordinator) are workers */
    return mpi->size - 1;
}

static int mpi_get_rank(const struct transport *t)
{
    if (!t || !t->impl) return -1;
    struct mpi_transport *mpi = (struct mpi_transport *)t->impl;
    return mpi->rank;
}

/* Vtable for MPI backend */
static struct {
    int (*shutdown)(struct transport *t);
    int (*send)(struct transport *t, const message_t *msg, int dst_rank);
    message_t* (*recv)(struct transport *t, int *src_rank);
    int (*poll)(struct transport *t, int timeout_ms);
    int (*broadcast)(struct transport *t, const message_t *msg);
    void (*get_stats)(const struct transport *t, transport_stats_t *stats);
    int (*worker_count)(const struct transport *t);
    int (*get_rank)(const struct transport *t);
} mpi_vtable = {
    .shutdown = mpi_shutdown,
    .send = mpi_send,
    .recv = mpi_recv,
    .poll = mpi_poll,
    .broadcast = mpi_broadcast,
    .get_stats = mpi_get_stats,
    .worker_count = mpi_worker_count,
    .get_rank = mpi_get_rank,
};

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

    /* Allocate MPI-specific structure */
    struct mpi_transport *mpi = calloc(1, sizeof(*mpi));
    if (!mpi) return -4;

    mpi->type = TRANSPORT_TYPE_MPI;
    mpi->role = config->role;
    mpi->timeout_ms = config->timeout_ms;
    mpi->comm = MPI_COMM_WORLD;

    MPI_Comm_rank(mpi->comm, &mpi->rank);
    MPI_Comm_size(mpi->comm, &mpi->size);

    /* Allocate generic transport wrapper */
    transport_t *t = calloc(1, sizeof(*t));
    if (!t) {
        free(mpi);
        return -4;
    }

    /* Allocate and copy vtable */
    struct {
        int (*shutdown)(struct transport *t);
        int (*send)(struct transport *t, const message_t *msg, int dst_rank);
        message_t* (*recv)(struct transport *t, int *src_rank);
        int (*poll)(struct transport *t, int timeout_ms);
        int (*broadcast)(struct transport *t, const message_t *msg);
        void (*get_stats)(const struct transport *t, transport_stats_t *stats);
        int (*worker_count)(const struct transport *t);
        int (*get_rank)(const struct transport *t);
    } *vtable = malloc(sizeof(*vtable));
    
    if (!vtable) {
        free(t);
        free(mpi);
        return -4;
    }

    memcpy(vtable, &mpi_vtable, sizeof(mpi_vtable));
    
    t->type = TRANSPORT_TYPE_MPI;
    t->vtable = (void *)vtable;
    t->impl = (void *)mpi;

    log_info("MPI transport initialized (rank=%d, size=%d, role=%s)",
             mpi->rank, mpi->size,
             mpi->role == TRANSPORT_ROLE_COORDINATOR ? "coordinator" : "worker");

    *out = t;
    return 0;
}