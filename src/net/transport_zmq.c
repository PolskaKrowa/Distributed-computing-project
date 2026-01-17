#include "transport.h"
#include "../common/log.h"
#include "../../include/project.h"

#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define MESSAGE_MAGIC 0xDA7A1E00
#define ENVELOPE_VERSION 1

#define DEFAULT_ENDPOINT "tcp://127.0.0.1:5555"
#define MAX_WORKERS 1024

/* Worker registry entry (coordinator only) */
typedef struct {
    char identity[64];
    uint64_t last_heartbeat;
    int active;
} worker_entry_t;

/* ZMQ-specific transport implementation structure */
struct zmq_transport {
    transport_type_t type;
    transport_role_t role;
    int timeout_ms;
    transport_stats_t stats;

    void *context;
    void *socket;
    char endpoint[256];

    /* Worker tracking (coordinator only) */
    worker_entry_t workers[MAX_WORKERS];
    int worker_count;
    pthread_mutex_t worker_lock;

    /* Local identity (worker only) */
    char identity[64];
    int worker_id;
};

/*
 * Helper: generate unique identity for worker
 */
static void generate_identity(char *buf, size_t len)
{
    snprintf(buf, len, "worker-%d-%lu", getpid(), (unsigned long)time(NULL));
}

/*
 * Helper: get current time in milliseconds
 */
static uint64_t current_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * Worker registry operations (coordinator only)
 */
static int zmq_register_worker(struct zmq_transport *zmq, const char *identity)
{
    pthread_mutex_lock(&zmq->worker_lock);

    /* Check if already registered */
    for (int i = 0; i < zmq->worker_count; i++) {
        if (strcmp(zmq->workers[i].identity, identity) == 0) {
            zmq->workers[i].last_heartbeat = current_time_ms();
            zmq->workers[i].active = 1;
            pthread_mutex_unlock(&zmq->worker_lock);
            log_debug("Worker %s already registered, updating heartbeat", identity);
            return i;
        }
    }

    /* Add new worker */
    if (zmq->worker_count >= MAX_WORKERS) {
        pthread_mutex_unlock(&zmq->worker_lock);
        log_error("Maximum worker count (%d) reached", MAX_WORKERS);
        return -1;
    }

    int idx = zmq->worker_count++;
    strncpy(zmq->workers[idx].identity, identity, sizeof(zmq->workers[idx].identity) - 1);
    zmq->workers[idx].last_heartbeat = current_time_ms();
    zmq->workers[idx].active = 1;

    pthread_mutex_unlock(&zmq->worker_lock);
    log_info("Worker %s registered (id=%d, total=%d)", identity, idx, zmq->worker_count);
    return idx;
}

static void zmq_update_heartbeat(struct zmq_transport *zmq, const char *identity)
{
    pthread_mutex_lock(&zmq->worker_lock);
    for (int i = 0; i < zmq->worker_count; i++) {
        if (strcmp(zmq->workers[i].identity, identity) == 0) {
            zmq->workers[i].last_heartbeat = current_time_ms();
            break;
        }
    }
    pthread_mutex_unlock(&zmq->worker_lock);
}

/* Backend-specific implementations (static, renamed to avoid ZMQ library conflicts) */

static int transport_zmq_shutdown(struct transport *t)
{
    if (!t || !t->impl) return -1;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;

    log_info("ZMQ transport shutting down (role=%s, sent=%lu, recv=%lu)",
             zmq->role == TRANSPORT_ROLE_COORDINATOR ? "coordinator" : "worker",
             zmq->stats.messages_sent, zmq->stats.messages_received);

    /* Workers send shutdown message */
    if (zmq->role == TRANSPORT_ROLE_WORKER) {
        message_t *shutdown_msg = message_alloc(0);
        if (shutdown_msg) {
            message_set_header(shutdown_msg, MSG_TYPE_WORKER_SHUTDOWN, 0);
            zmq_send(zmq->socket, &shutdown_msg->header, sizeof(message_header_t), 0);
            message_free(shutdown_msg);
        }
    }

    zmq_close(zmq->socket);
    zmq_ctx_destroy(zmq->context);
    pthread_mutex_destroy(&zmq->worker_lock);
    free(zmq);
    free(t->vtable);
    free(t);

    return 0;
}

static int transport_zmq_send(struct transport *t, const message_t *msg, int dst_rank)
{
    if (!t || !t->impl || !msg) return -1;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;
    
    if (message_validate(msg) != 0) return -2;

    int rc;

    if (zmq->role == TRANSPORT_ROLE_COORDINATOR) {
        /* Coordinator must specify worker identity */
        if (dst_rank < 0 || dst_rank >= zmq->worker_count) {
            log_error("Invalid dst_rank %d (worker_count=%d)", dst_rank, zmq->worker_count);
            return -3;
        }

        pthread_mutex_lock(&zmq->worker_lock);
        const char *identity = zmq->workers[dst_rank].identity;
        
        /* Send identity frame */
        rc = zmq_send(zmq->socket, (void *)identity, strlen(identity), ZMQ_SNDMORE);
        if (rc < 0) {
            pthread_mutex_unlock(&zmq->worker_lock);
            log_error("zmq_send(identity) failed: %s", zmq_strerror(errno));
            zmq->stats.errors++;
            return -4;
        }
        pthread_mutex_unlock(&zmq->worker_lock);

    } /* Workers don't need to send identity - DEALER handles it */

    /* Send header */
    rc = zmq_send(zmq->socket, (void *)&msg->header, sizeof(message_header_t), 
                  msg->header.payload_len > 0 ? ZMQ_SNDMORE : 0);
    if (rc < 0) {
        log_error("zmq_send(header) failed: %s", zmq_strerror(errno));
        zmq->stats.errors++;
        return -5;
    }

    /* Send payload if present */
    if (msg->header.payload_len > 0 && msg->payload) {
        rc = zmq_send(zmq->socket, msg->payload, msg->header.payload_len, 0);
        if (rc < 0) {
            log_error("zmq_send(payload) failed: %s", zmq_strerror(errno));
            zmq->stats.errors++;
            return -6;
        }
    }

    zmq->stats.messages_sent++;
    zmq->stats.bytes_sent += sizeof(message_header_t) + msg->header.payload_len;

    log_debug("ZMQ sent message type=%s (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type),
              msg->header.task_id, msg->header.payload_len);

    return 0;
}

static message_t *transport_zmq_recv(struct transport *t, int *src_rank)
{
    if (!t || !t->impl) return NULL;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;

    /* Set receive timeout */
    int timeout = zmq->timeout_ms;
    if (zmq_setsockopt(zmq->socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        log_error("zmq_setsockopt(RCVTIMEO) failed");
        return NULL;
    }

    char identity[64] = {0};
    int identity_len = 0;

    /* Coordinator receives identity frame first */
    if (zmq->role == TRANSPORT_ROLE_COORDINATOR) {
        int rc = zmq_recv(zmq->socket, identity, sizeof(identity) - 1, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                log_error("zmq_recv(identity) failed: %s", zmq_strerror(errno));
                zmq->stats.errors++;
            }
            return NULL; /* timeout or error */
        }
        identity_len = rc;
        identity[identity_len] = '\0';
    }

    /* Receive header */
    message_header_t header;
    int rc = zmq_recv(zmq->socket, &header, sizeof(header), 0);
    if (rc < 0) {
        if (errno != EAGAIN) {
            log_error("zmq_recv(header) failed: %s", zmq_strerror(errno));
            zmq->stats.errors++;
        }
        return NULL;
    }
    if (rc != (int)sizeof(header)) {
        log_error("Received partial header (%d bytes)", rc);
        zmq->stats.errors++;
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
        rc = zmq_recv(zmq->socket, msg->payload, header.payload_len, 0);
        if (rc < 0) {
            log_error("zmq_recv(payload) failed: %s", zmq_strerror(errno));
            message_free(msg);
            zmq->stats.errors++;
            return NULL;
        }
        if ((uint32_t)rc != header.payload_len) {
            log_error("Received partial payload (%d/%u bytes)", rc, header.payload_len);
            message_free(msg);
            zmq->stats.errors++;
            return NULL;
        }
    }

    /* Validate message */
    if (message_validate(msg) != 0) {
        log_error("Received invalid message");
        message_free(msg);
        return NULL;
    }

    /* Handle special message types for coordinator */
    if (zmq->role == TRANSPORT_ROLE_COORDINATOR && identity_len > 0) {
        if (msg->header.msg_type == MSG_TYPE_WORKER_REGISTER) {
            int id = zmq_register_worker(zmq, identity);
            if (src_rank) *src_rank = id;
        } else if (msg->header.msg_type == MSG_TYPE_HEARTBEAT) {
            zmq_update_heartbeat(zmq, identity);
        }

        /* Map identity to worker ID */
        if (src_rank) {
            pthread_mutex_lock(&zmq->worker_lock);
            for (int i = 0; i < zmq->worker_count; i++) {
                if (strcmp(zmq->workers[i].identity, identity) == 0) {
                    *src_rank = i;
                    break;
                }
            }
            pthread_mutex_unlock(&zmq->worker_lock);
        }
    }

    zmq->stats.messages_received++;
    zmq->stats.bytes_received += sizeof(message_header_t) + header.payload_len;

    log_debug("ZMQ received message type=%s (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type),
              msg->header.task_id, msg->header.payload_len);

    return msg;
}

static int transport_zmq_poll(struct transport *t, int timeout_ms)
{
    if (!t || !t->impl) return -1;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;

    zmq_pollitem_t items[] = {
        { zmq->socket, 0, ZMQ_POLLIN, 0 }
    };

    int rc = zmq_poll(items, 1, timeout_ms);
    if (rc < 0) {
        log_error("zmq_poll failed: %s", zmq_strerror(errno));
        return -2;
    }

    return (items[0].revents & ZMQ_POLLIN) ? 1 : 0;
}

static int transport_zmq_broadcast(struct transport *t, const message_t *msg)
{
    if (!t || !t->impl || !msg) return -1;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;
    
    if (zmq->role != TRANSPORT_ROLE_COORDINATOR) {
        log_error("Only coordinator can broadcast");
        return -2;
    }

    pthread_mutex_lock(&zmq->worker_lock);
    int errors = 0;
    for (int i = 0; i < zmq->worker_count; i++) {
        if (zmq->workers[i].active) {
            if (transport_zmq_send(t, msg, i) != 0) {
                errors++;
            }
        }
    }
    pthread_mutex_unlock(&zmq->worker_lock);

    return errors > 0 ? -3 : 0;
}

static void transport_zmq_get_stats(const struct transport *t, transport_stats_t *stats)
{
    if (!t || !t->impl || !stats) return;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;
    memcpy(stats, &zmq->stats, sizeof(*stats));
}

static int transport_zmq_worker_count(const struct transport *t)
{
    if (!t || !t->impl) return -1;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;
    
    if (zmq->role != TRANSPORT_ROLE_COORDINATOR) return -2;
    
    pthread_mutex_lock(&zmq->worker_lock);
    int count = zmq->worker_count;
    pthread_mutex_unlock(&zmq->worker_lock);
    
    return count;
}

static int transport_zmq_get_rank(const struct transport *t)
{
    if (!t || !t->impl) return -1;
    struct zmq_transport *zmq = (struct zmq_transport *)t->impl;
    return zmq->worker_id;
}

/* Vtable for ZMQ backend */
static struct {
    int (*shutdown)(struct transport *t);
    int (*send)(struct transport *t, const message_t *msg, int dst_rank);
    message_t* (*recv)(struct transport *t, int *src_rank);
    int (*poll)(struct transport *t, int timeout_ms);
    int (*broadcast)(struct transport *t, const message_t *msg);
    void (*get_stats)(const struct transport *t, transport_stats_t *stats);
    int (*worker_count)(const struct transport *t);
    int (*get_rank)(const struct transport *t);
} zmq_vtable = {
    .shutdown = transport_zmq_shutdown,
    .send = transport_zmq_send,
    .recv = transport_zmq_recv,
    .poll = transport_zmq_poll,
    .broadcast = transport_zmq_broadcast,
    .get_stats = transport_zmq_get_stats,
    .worker_count = transport_zmq_worker_count,
    .get_rank = transport_zmq_get_rank,
};

int transport_init_zmq(transport_t **out, const transport_config_t *config)
{
    if (!out || !config) return -1;

    /* Allocate ZMQ-specific structure */
    struct zmq_transport *zmq = calloc(1, sizeof(*zmq));
    if (!zmq) return -3;

    zmq->type = TRANSPORT_TYPE_ZMQ;
    zmq->role = config->role;
    zmq->timeout_ms = config->timeout_ms;
    pthread_mutex_init(&zmq->worker_lock, NULL);

    /* Create ZeroMQ context */
    zmq->context = zmq_ctx_new();
    if (!zmq->context) {
        log_error("zmq_ctx_new failed");
        free(zmq);
        return -4;
    }

    /* Set endpoint */
    const char *endpoint = config->endpoint ? config->endpoint : DEFAULT_ENDPOINT;
    strncpy(zmq->endpoint, endpoint, sizeof(zmq->endpoint) - 1);

    if (zmq->role == TRANSPORT_ROLE_COORDINATOR) {
        /* Coordinator uses ROUTER socket for bidirectional communication */
        zmq->socket = zmq_socket(zmq->context, ZMQ_ROUTER);
        if (!zmq->socket) {
            log_error("zmq_socket(ROUTER) failed");
            zmq_ctx_destroy(zmq->context);
            free(zmq);
            return -5;
        }

        /* Bind to endpoint */
        if (zmq_bind(zmq->socket, zmq->endpoint) != 0) {
            log_error("zmq_bind(%s) failed: %s", zmq->endpoint, zmq_strerror(errno));
            zmq_close(zmq->socket);
            zmq_ctx_destroy(zmq->context);
            free(zmq);
            return -6;
        }

        log_info("ZMQ coordinator bound to %s", zmq->endpoint);

    } else {
        /* Worker uses DEALER socket */
        zmq->socket = zmq_socket(zmq->context, ZMQ_DEALER);
        if (!zmq->socket) {
            log_error("zmq_socket(DEALER) failed");
            zmq_ctx_destroy(zmq->context);
            free(zmq);
            return -7;
        }

        /* Set identity */
        generate_identity(zmq->identity, sizeof(zmq->identity));
        if (zmq_setsockopt(zmq->socket, ZMQ_IDENTITY, zmq->identity, 
                          strlen(zmq->identity)) != 0) {
            log_error("zmq_setsockopt(IDENTITY) failed");
            zmq_close(zmq->socket);
            zmq_ctx_destroy(zmq->context);
            free(zmq);
            return -8;
        }

        /* Connect to coordinator */
        if (zmq_connect(zmq->socket, zmq->endpoint) != 0) {
            log_error("zmq_connect(%s) failed: %s", zmq->endpoint, zmq_strerror(errno));
            zmq_close(zmq->socket);
            zmq_ctx_destroy(zmq->context);
            free(zmq);
            return -9;
        }

        log_info("ZMQ worker %s connected to %s", zmq->identity, zmq->endpoint);
    }

    /* Allocate generic transport wrapper */
    transport_t *t = calloc(1, sizeof(*t));
    if (!t) {
        zmq_close(zmq->socket);
        zmq_ctx_destroy(zmq->context);
        free(zmq);
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
        zmq_close(zmq->socket);
        zmq_ctx_destroy(zmq->context);
        free(zmq);
        return -4;
    }

    memcpy(vtable, &zmq_vtable, sizeof(zmq_vtable));
    
    t->type = TRANSPORT_TYPE_ZMQ;
    t->vtable = (void *)vtable;
    t->impl = (void *)zmq;

    *out = t;
    return 0;
}
