#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

typedef struct transport transport_t;
typedef struct transport_conn transport_conn_t;

// Listener
int transport_listen(const char *bind_addr, uint16_t port, transport_t **out);
int transport_accept(transport_t *t, transport_conn_t **out);

// Client
int transport_connect(const char *host, uint16_t port, transport_conn_t **out);

// Send/recv framed message. Payload is user-owned on send. On receive *payload is malloced and must be free()d by caller.
int transport_send_message(transport_conn_t *c, uint32_t type, const void *payload, uint32_t payload_len);
int transport_recv_message(transport_conn_t *c, uint32_t *out_type, void **out_payload, uint32_t *out_payload_len);

const char *transport_conn_peer(const transport_conn_t *c);

// Close
int transport_close_conn(transport_conn_t *c);
int transport_shutdown(transport_t *t);

// Message types used by the test harness
enum {
    MSG_REGISTER = 1,
    MSG_REGISTER_ACK = 2,
    MSG_HEARTBEAT = 3,
    MSG_TASK = 4,
    MSG_RESULT = 5
};

#endif // TRANSPORT_H