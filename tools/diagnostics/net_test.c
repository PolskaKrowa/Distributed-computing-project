#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include "../../include/transport.h"

static void usage(const char *p) {
    fprintf(stderr, "Usage:\n  %s coordinator <port>\n  %s worker <host> <port>\n", p, p);
}

int run_coordinator(uint16_t port) {
    transport_t *t = NULL;
    if (transport_listen(NULL, port, &t) != 0) {
        fprintf(stderr, "failed to listen on port %u\n", port);
        return 1;
    }
    printf("Coordinator listening on port %u\n", port);
    transport_conn_t *c = NULL;
    if (transport_accept(t, &c) != 0) {
        fprintf(stderr, "accept failed\n");
        transport_shutdown(t);
        return 1;
    }
    printf(
        "Accepted connection from %s\n",
        transport_conn_peer(c) ? transport_conn_peer(c) : "(unknown)"
    );

    // Expect REGISTER
    uint32_t type; void *payload; uint32_t len;
    if (transport_recv_message(c, &type, &payload, &len) != 0) {
        fprintf(stderr, "failed to recv register\n");
        transport_close_conn(c); transport_shutdown(t); return 1;
    }
    if (type != MSG_REGISTER) {
        fprintf(stderr, "expected REGISTER (%d) got %u\n", MSG_REGISTER, type);
    } else {
        printf("Worker registered: %s\n", payload ? (char*)payload : "(no payload)");
    }
    free(payload);

    // Send REGISTER_ACK
    const char ack[] = "ok";
    transport_send_message(c, MSG_REGISTER_ACK, ack, sizeof(ack)-1);

    // Send TASK
    const char task[] = "compute: example task";
    transport_send_message(c, MSG_TASK, task, sizeof(task)-1);
    printf("Task sent\n");

    // Wait for RESULT
    if (transport_recv_message(c, &type, &payload, &len) != 0) {
        fprintf(stderr, "failed to recv result\n");
        transport_close_conn(c); transport_shutdown(t); return 1;
    }
    if (type != MSG_RESULT) {
        fprintf(stderr, "expected RESULT (%d) got %u\n", MSG_RESULT, type);
    } else {
        printf("Received result from worker: %s\n", payload ? (char*)payload : "(no payload)");
    }
    free(payload);

    transport_close_conn(c);
    transport_shutdown(t);
    return 0;
}

int run_worker(const char *host, uint16_t port) {
    transport_conn_t *c = NULL;
    if (transport_connect(host, port, &c) != 0) {
        fprintf(stderr, "failed to connect to %s:%u\n", host, port);
        return 1;
    }
    printf("Worker connected to %s:%u\n", host, port);

    const char id[] = "worker-1";
    transport_send_message(c, MSG_REGISTER, id, sizeof(id)-1);

    uint32_t type; void *payload; uint32_t len;
    if (transport_recv_message(c, &type, &payload, &len) != 0) {
        fprintf(stderr, "failed to recv\n"); transport_close_conn(c); return 1;
    }
    if (type == MSG_REGISTER_ACK) {
        printf("Registered OK from coordinator\n");
    }
    free(payload);

    // Wait for TASK
    if (transport_recv_message(c, &type, &payload, &len) != 0) {
        fprintf(stderr, "failed to recv task\n"); transport_close_conn(c); return 1;
    }
    if (type != MSG_TASK) {
        fprintf(stderr, "expected TASK got %u\n", type);
    } else {
        printf("Received task: %s\n", payload ? (char*)payload : "(no payload)");
    }
    free(payload);

    // Send RESULT
    const char result[] = "result: 42";
    transport_send_message(c, MSG_RESULT, result, sizeof(result)-1);
    printf("Result sent\n");

    transport_close_conn(c);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    if (strcmp(argv[1], "coordinator") == 0) {
        if (argc != 3) { usage(argv[0]); return 1; }
        uint16_t port = (uint16_t)atoi(argv[2]);
        return run_coordinator(port);
    } else if (strcmp(argv[1], "worker") == 0) {
        if (argc != 4) { usage(argv[0]); return 1; }
        const char *host = argv[2];
        uint16_t port = (uint16_t)atoi(argv[3]);
        return run_worker(host, port);
    } else {
        usage(argv[0]); return 1;
    }
}