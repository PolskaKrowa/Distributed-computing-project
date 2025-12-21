#include "transport.h"
#include "../common/log.h"

#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <pthread.h>
#include <unistd.h>

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

/* Transport implementation for ZeroMQ */
struct transport {
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
static int register_worker(transport_t *t, const char *identity)
{
    pthread_mutex_lock(&t->worker_lock);

    /* Check if already registered */
    for (int i = 0; i < t->worker_count; i++) {
        if (strcmp(t->workers[i].identity, identity) == 0) {
            t->workers[i].last_heartbeat = current_time_ms();
            t->workers[i].active = 1;
            pthread_mutex_unlock(&t->worker_lock);
            log_debug("Worker %s already registered, updating heartbeat", identity);
            return i;
        }
    }

    /* Add new worker */
    if (t->worker_count >= MAX_WORKERS) {
        pthread_mutex_unlock(&t->worker_lock);
        log_error("Maximum worker count (%d) reached", MAX_WORKERS);
        return -1;
    }

    int idx = t->worker_count++;
    strncpy(t->workers[idx].identity, identity, sizeof(t->workers[idx].identity) - 1);
    t->workers[idx].last_heartbeat = current_time_ms();
    t->workers[idx].active = 1;

    pthread_mutex_unlock(&t->worker_lock);
    log_info("Worker %s registered (id=%d, total=%d)", identity, idx, t->worker_count);
    return idx;
}

static void update_heartbeat(transport_t *t, const char *identity)
{
    pthread_mutex_lock(&t->worker_lock);
    for (int i = 0; i < t->worker_count; i++) {
        if (strcmp(t->workers[i].identity, identity) == 0) {
            t->workers[i].last_heartbeat = current_time_ms();
            break;
        }
    }
    pthread_mutex_unlock(&t->worker_lock);
}

int transport_init_zmq(transport_t **out, const transport_config_t *config)
{
    if (!out || !config) return -1;

    transport_t *t = calloc(1, sizeof(*t));
    if (!t) return -3;

    t->type = TRANSPORT_TYPE_ZMQ;
    t->role = config->role;
    t->timeout_ms = config->timeout_ms;
    pthread_mutex_init(&t->worker_lock, NULL);

    /* Create ZeroMQ context */
    t->context = zmq_ctx_new();
    if (!t->context) {
        log_error("zmq_ctx_new failed");
        free(t);
        return -4;
    }

    /* Set endpoint */
    const char *endpoint = config->endpoint ? config->endpoint : DEFAULT_ENDPOINT;
    strncpy(t->endpoint, endpoint, sizeof(t->endpoint) - 1);

    if (t->role == TRANSPORT_ROLE_COORDINATOR) {
        /* Coordinator uses ROUTER socket for bidirectional communication */
        t->socket = zmq_socket(t->context, ZMQ_ROUTER);
        if (!t->socket) {
            log_error("zmq_socket(ROUTER) failed");
            zmq_ctx_destroy(t->context);
            free(t);
            return -5;
        }

        /* Bind to endpoint */
        if (zmq_bind(t->socket, t->endpoint) != 0) {
            log_error("zmq_bind(%s) failed: %s", t->endpoint, zmq_strerror(errno));
            zmq_close(t->socket);
            zmq_ctx_destroy(t->context);
            free(t);
            return -6;
        }

        log_info("ZMQ coordinator bound to %s", t->endpoint);

    } else {
        /* Worker uses DEALER socket */
        t->socket = zmq_socket(t->context, ZMQ_DEALER);
        if (!t->socket) {
            log_error("zmq_socket(DEALER) failed");
            zmq_ctx_destroy(t->context);
            free(t);
            return -7;
        }

        /* Set identity */
        generate_identity(t->identity, sizeof(t->identity));
        if (zmq_setsockopt(t->socket, ZMQ_IDENTITY, t->identity, 
                          strlen(t->identity)) != 0) {
            log_error("zmq_setsockopt(IDENTITY) failed");
            zmq_close(t->socket);
            zmq_ctx_destroy(t->context);
            free(t);
            return -8;
        }

        /* Connect to coordinator */
        if (zmq_connect(t->socket, t->endpoint) != 0) {
            log_error("zmq_connect(%s) failed: %s", t->endpoint, zmq_strerror(errno));
            zmq_close(t->socket);
            zmq_ctx_destroy(t->context);
            free(t);
            return -9;
        }

        log_info("ZMQ worker %s connected to %s", t->identity, t->endpoint);

        /* Send registration message */
        message_t *reg_msg = message_alloc(0);
        if (reg_msg) {
            message_set_header(reg_msg, MSG_TYPE_WORKER_REGISTER, 0);
            transport_send(t, reg_msg, 0);
            message_free(reg_msg);
        }
    }

    *out = t;
    return 0;
}

int transport_shutdown(transport_t *t)
{
    if (!t) return -1;

    log_info("ZMQ transport shutting down (role=%s, sent=%lu, recv=%lu)",
             t->role == TRANSPORT_ROLE_COORDINATOR ? "coordinator" : "worker",
             t->stats.messages_sent, t->stats.messages_received);

    /* Workers send shutdown message */
    if (t->role == TRANSPORT_ROLE_WORKER) {
        message_t *shutdown_msg = message_alloc(0);
        if (shutdown_msg) {
            message_set_header(shutdown_msg, MSG_TYPE_WORKER_SHUTDOWN, 0);
            transport_send(t, shutdown_msg, 0);
            message_free(shutdown_msg);
        }
    }

    zmq_close(t->socket);
    zmq_ctx_destroy(t->context);
    pthread_mutex_destroy(&t->worker_lock);
    free(t);

    return 0;
}

int transport_send(transport_t *t, const message_t *msg, int dst_rank)
{
    if (!t || !msg) return -1;
    if (message_validate(msg) != 0) return -2;

    int rc;

    if (t->role == TRANSPORT_ROLE_COORDINATOR) {
        /* Coordinator must specify worker identity */
        if (dst_rank < 0 || dst_rank >= t->worker_count) {
            log_error("Invalid dst_rank %d (worker_count=%d)", dst_rank, t->worker_count);
            return -3;
        }

        pthread_mutex_lock(&t->worker_lock);
        const char *identity = t->workers[dst_rank].identity;
        
        /* Send identity frame */
        rc = zmq_send(t->socket, identity, strlen(identity), ZMQ_SNDMORE);
        if (rc < 0) {
            pthread_mutex_unlock(&t->worker_lock);
            log_error("zmq_send(identity) failed: %s", zmq_strerror(errno));
            t->stats.errors++;
            return -4;
        }
        pthread_mutex_unlock(&t->worker_lock);

    } /* Workers don't need to send identity - DEALER handles it */

    /* Send header */
    rc = zmq_send(t->socket, &msg->header, sizeof(message_header_t), 
                  msg->header.payload_len > 0 ? ZMQ_SNDMORE : 0);
    if (rc < 0) {
        log_error("zmq_send(header) failed: %s", zmq_strerror(errno));
        t->stats.errors++;
        return -5;
    }

    /* Send payload if present */
    if (msg->header.payload_len > 0 && msg->payload) {
        rc = zmq_send(t->socket, msg->payload, msg->header.payload_len, 0);
        if (rc < 0) {
            log_error("zmq_send(payload) failed: %s", zmq_strerror(errno));
            t->stats.errors++;
            return -6;
        }
    }

    t->stats.messages_sent++;
    t->stats.bytes_sent += sizeof(message_header_t) + msg->header.payload_len;

    log_debug("ZMQ sent message type=%s (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type),
              msg->header.task_id, msg->header.payload_len);

    return 0;
}

message_t *transport_recv(transport_t *t, int *src_rank)
{
    if (!t) return NULL;

    /* Set receive timeout */
    int timeout = t->timeout_ms;
    if (zmq_setsockopt(t->socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        log_error("zmq_setsockopt(RCVTIMEO) failed");
        return NULL;
    }

    char identity[64] = {0};
    int identity_len = 0;

    /* Coordinator receives identity frame first */
    if (t->role == TRANSPORT_ROLE_COORDINATOR) {
        int rc = zmq_recv(t->socket, identity, sizeof(identity) - 1, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                log_error("zmq_recv(identity) failed: %s", zmq_strerror(errno));
                t->stats.errors++;
            }
            return NULL; /* timeout or error */
        }
        identity_len = rc;
        identity[identity_len] = '\0';
    }

    /* Receive header */
    message_header_t header;
    int rc = zmq_recv(t->socket, &header, sizeof(header), 0);
    if (rc < 0) {
        if (errno != EAGAIN) {
            log_error("zmq_recv(header) failed: %s", zmq_strerror(errno));
            t->stats.errors++;
        }
        return NULL;
    }
    if (rc != sizeof(header)) {
        log_error("Received partial header (%d bytes)", rc);
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
        rc = zmq_recv(t->socket, msg->payload, header.payload_len, 0);
        if (rc < 0) {
            log_error("zmq_recv(payload) failed: %s", zmq_strerror(errno));
            message_free(msg);
            t->stats.errors++;
            return NULL;
        }
        if ((uint32_t)rc != header.payload_len) {
            log_error("Received partial payload (%d/%u bytes)", rc, header.payload_len);
            message_free(msg);
            t->stats.errors++;
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
    if (t->role == TRANSPORT_ROLE_COORDINATOR && identity_len > 0) {
        if (msg->header.msg_type == MSG_TYPE_WORKER_REGISTER) {
            int id = register_worker(t, identity);
            if (src_rank) *src_rank = id;
        } else if (msg->header.msg_type == MSG_TYPE_HEARTBEAT) {
            update_heartbeat(t, identity);
        }

        /* Map identity to worker ID */
        if (src_rank) {
            pthread_mutex_lock(&t->worker_lock);
            for (int i = 0; i < t->worker_count; i++) {
                if (strcmp(t->workers[i].identity, identity) == 0) {
                    *src_rank = i;
                    break;
                }
            }
            pthread_mutex_unlock(&t->worker_lock);
        }
    }

    t->stats.messages_received++;
    t->stats.bytes_received += sizeof(message_header_t) + header.payload_len;

    log_debug("ZMQ received message type=%s (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type),
              msg->header.task_id, msg->header.payload_len);

    return msg;
}

int transport_poll(transport_t *t, int timeout_ms)
{
    if (!t) return -1;

    zmq_pollitem_t items[] = {
        { t->socket, 0, ZMQ_POLLIN, 0 }
    };

    int rc = zmq_poll(items, 1, timeout_ms);
    if (rc < 0) {
        log_error("zmq_poll failed: %s", zmq_strerror(errno));
        return -2;
    }

    return (items[0].revents & ZMQ_POLLIN) ? 1 : 0;
}

int transport_broadcast(transport_t *t, const message_t *msg)
{
    if (!t || !msg) return -1;
    if (t->role != TRANSPORT_ROLE_COORDINATOR) {
        log_error("Only coordinator can broadcast");
        return -2;
    }

    pthread_mutex_lock(&t->worker_lock);
    int errors = 0;
    for (int i = 0; i < t->worker_count; i++) {
        if (t->workers[i].active) {
            if (transport_send(t, msg, i) != 0) {
                errors++;
            }
        }
    }
    pthread_mutex_unlock(&t->worker_lock);

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
    
    pthread_mutex_lock((pthread_mutex_t*)&t->worker_lock);
    int count = t->worker_count;
    pthread_mutex_unlock((pthread_mutex_t*)&t->worker_lock);
    
    return count;
}

int transport_get_rank(const transport_t *t)
{
    return t ? t->worker_id : -1;
}

int transport_shutdown(transport_t *t)
{
    if (!t) return -1;

    log_info("ZMQ transport shutting down (role=%s, sent=%lu, recv=%lu)",
             t->role == TRANSPORT_ROLE_COORDINATOR ? "coordinator" : "worker",
             t->stats.messages_sent, t->stats.messages_received);

    /* Workers send shutdown message */
    if (t->role == TRANSPORT_ROLE_WORKER) {
        message_t *shutdown_msg = message_alloc(0);
        if (shutdown_msg) {
            message_set_header(shutdown_msg, MSG_TYPE_WORKER_SHUTDOWN, 0);
            transport_send(t, shutdown_msg, 0);
            message_free(shutdown_msg);
        }
    }

    zmq_close(t->socket);
    zmq_ctx_destroy(t->context);
    pthread_mutex_destroy(&t->worker_lock);
    free(t);

    return 0;
}

int transport_send(transport_t *t, const message_t *msg, int dst_rank)
{
    if (!t || !msg) return -1;
    if (message_validate(msg) != 0) return -2;

    int rc;

    if (t->role == TRANSPORT_ROLE_COORDINATOR) {
        /* Coordinator must specify worker identity */
        if (dst_rank < 0 || dst_rank >= t->worker_count) {
            log_error("Invalid dst_rank %d (worker_count=%d)", dst_rank, t->worker_count);
            return -3;
        }

        pthread_mutex_lock(&t->worker_lock);
        const char *identity = t->workers[dst_rank].identity;
        
        /* Send identity frame */
        rc = zmq_send(t->socket, identity, strlen(identity), ZMQ_SNDMORE);
        if (rc < 0) {
            pthread_mutex_unlock(&t->worker_lock);
            log_error("zmq_send(identity) failed: %s", zmq_strerror(errno));
            t->stats.errors++;
            return -4;
        }
        pthread_mutex_unlock(&t->worker_lock);

    } /* Workers don't need to send identity - DEALER handles it */

    /* Send header */
    rc = zmq_send(t->socket, &msg->header, sizeof(message_header_t), 
                  msg->header.payload_len > 0 ? ZMQ_SNDMORE : 0);
    if (rc < 0) {
        log_error("zmq_send(header) failed: %s", zmq_strerror(errno));
        t->stats.errors++;
        return -5;
    }

    /* Send payload if present */
    if (msg->header.payload_len > 0 && msg->payload) {
        rc = zmq_send(t->socket, msg->payload, msg->header.payload_len, 0);
        if (rc < 0) {
            log_error("zmq_send(payload) failed: %s", zmq_strerror(errno));
            t->stats.errors++;
            return -6;
        }
    }

    t->stats.messages_sent++;
    t->stats.bytes_sent += sizeof(message_header_t) + msg->header.payload_len;

    log_debug("ZMQ sent message type=%s (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type),
              msg->header.task_id, msg->header.payload_len);

    return 0;
}

message_t *transport_recv(transport_t *t, int *src_rank)
{
    if (!t) return NULL;

    /* Set receive timeout */
    int timeout = t->timeout_ms;
    if (zmq_setsockopt(t->socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        log_error("zmq_setsockopt(RCVTIMEO) failed");
        return NULL;
    }

    char identity[64] = {0};
    int identity_len = 0;

    /* Coordinator receives identity frame first */
    if (t->role == TRANSPORT_ROLE_COORDINATOR) {
        int rc = zmq_recv(t->socket, identity, sizeof(identity) - 1, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                log_error("zmq_recv(identity) failed: %s", zmq_strerror(errno));
                t->stats.errors++;
            }
            return NULL; /* timeout or error */
        }
        identity_len = rc;
        identity[identity_len] = '\0';
    }

    /* Receive header */
    message_header_t header;
    int rc = zmq_recv(t->socket, &header, sizeof(header), 0);
    if (rc < 0) {
        if (errno != EAGAIN) {
            log_error("zmq_recv(header) failed: %s", zmq_strerror(errno));
            t->stats.errors++;
        }
        return NULL;
    }
    if (rc != sizeof(header)) {
        log_error("Received partial header (%d bytes)", rc);
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
        rc = zmq_recv(t->socket, msg->payload, header.payload_len, 0);
        if (rc < 0) {
            log_error("zmq_recv(payload) failed: %s", zmq_strerror(errno));
            message_free(msg);
            t->stats.errors++;
            return NULL;
        }
        if ((uint32_t)rc != header.payload_len) {
            log_error("Received partial payload (%d/%u bytes)", rc, header.payload_len);
            message_free(msg);
            t->stats.errors++;
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
    if (t->role == TRANSPORT_ROLE_COORDINATOR && identity_len > 0) {
        if (msg->header.msg_type == MSG_TYPE_WORKER_REGISTER) {
            int id = register_worker(t, identity);
            if (src_rank) *src_rank = id;
        } else if (msg->header.msg_type == MSG_TYPE_HEARTBEAT) {
            update_heartbeat(t, identity);
        }

        /* Map identity to worker ID */
        if (src_rank) {
            pthread_mutex_lock(&t->worker_lock);
            for (int i = 0; i < t->worker_count; i++) {
                if (strcmp(t->workers[i].identity, identity) == 0) {
                    *src_rank = i;
                    break;
                }
            }
            pthread_mutex_unlock(&t->worker_lock);
        }
    }

    t->stats.messages_received++;
    t->stats.bytes_received += sizeof(message_header_t) + header.payload_len;

    log_debug("ZMQ received message type=%s (task=%lu, payload=%u bytes)",
              message_type_to_string(msg->header.msg_type),
              msg->header.task_id, msg->header.payload_len);

    return msg;
}

int transport_poll(transport_t *t, int timeout_ms)
{
    if (!t) return -1;

    zmq_pollitem_t items[] = {
        { t->socket, 0, ZMQ_POLLIN, 0 }
    };

    int rc = zmq_poll(items, 1, timeout_ms);
    if (rc < 0) {
        log_error("zmq_poll failed: %s", zmq_strerror(errno));
        return -2;
    }

    return (items[0].revents & ZMQ_POLLIN) ? 1 : 0;
}

int transport_broadcast(transport_t *t, const message_t *msg)
{
    if (!t || !msg) return -1;
    if (t->role != TRANSPORT_ROLE_COORDINATOR) {
        log_error("Only coordinator can broadcast");
        return -2;
    }

    pthread_mutex_lock(&t->worker_lock);
    int errors = 0;
    for (int i = 0; i < t->worker_count; i++) {
        if (t->workers[i].active) {
            if (transport_send(t, msg, i) != 0) {
                errors++;
            }
        }
    }
    pthread_mutex_unlock(&t->worker_lock);

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
    
    pthread_mutex_lock((pthread_mutex_t*)&t->worker_lock);
    int count = t->worker_count;
    pthread_mutex_unlock((pthread_mutex_t*)&t->worker_lock);
    
    return count;
}