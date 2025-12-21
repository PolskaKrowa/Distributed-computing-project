/*
 * Example usage of the transport layer
 * 
 * This demonstrates both coordinator and worker patterns
 * for the distributed computing platform.
 *
 * Compile:
 *   # For MPI:
 *   mpicc transport_example.c transport_mpi.c ../common/log.c -o example_mpi
 *
 *   # For ZeroMQ:
 *   gcc transport_example.c transport_zmq.c ../common/log.c -lzmq -lpthread -o example_zmq
 */

#include "../../src/net/transport.h"
#include "../../src/common/log.h"
#include "../../src/common/errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Example: Simple task payload */
typedef struct {
    uint64_t task_id;
    double input_value;
    int iterations;
} task_payload_t;

typedef struct {
    uint64_t task_id;
    double result;
    int status;
} result_payload_t;

/*
 * Coordinator example
 */
void run_coordinator(transport_type_t type, const char *endpoint)
{
    log_info("Starting coordinator (transport=%s)", transport_type_to_string(type));

    /* Initialize transport */
    transport_config_t config = {
        .type = type,
        .role = TRANSPORT_ROLE_COORDINATOR,
        .endpoint = endpoint,
        .timeout_ms = 5000,
        .max_workers = 16
    };

    transport_t *transport;
    if (transport_init(&transport, &config) != 0) {
        log_error("Failed to initialise transport");
        return;
    }

    /* Wait for workers to connect */
    log_info("Waiting for workers to register...");
    sleep(2);

    int worker_count = transport_worker_count(transport);
    log_info("Connected workers: %d", worker_count);

    if (worker_count == 0) {
        log_warn("No workers available");
        transport_shutdown(transport);
        return;
    }

    /* Distribute tasks */
    const int num_tasks = 10;
    log_info("Distributing %d tasks to %d workers", num_tasks, worker_count);

    for (int i = 0; i < num_tasks; i++) {
        /* Create task */
        message_t *task_msg = message_alloc(sizeof(task_payload_t));
        if (!task_msg) {
            log_error("Failed to allocate task message");
            continue;
        }

        task_payload_t *payload = (task_payload_t *)task_msg->payload;
        payload->task_id = i + 1;
        payload->input_value = 1.0 + i * 0.5;
        payload->iterations = 1000 + i * 100;

        message_set_header(task_msg, MSG_TYPE_TASK_SUBMIT, payload->task_id);
        task_msg->header.payload_len = sizeof(task_payload_t);

        /* Round-robin distribution */
        int worker = i % worker_count;
        if (transport_send(transport, task_msg, worker) == 0) {
            log_info("Task %lu assigned to worker %d", payload->task_id, worker);
        } else {
            log_error("Failed to send task %lu", payload->task_id);
        }

        message_free(task_msg);
    }

    /* Collect results */
    log_info("Collecting results...");
    int results_received = 0;

    while (results_received < num_tasks) {
        int src_rank;
        message_t *result_msg = transport_recv(transport, &src_rank);

        if (!result_msg) {
            log_warn("No result received (timeout)");
            continue;
        }

        if (result_msg->header.msg_type == MSG_TYPE_TASK_RESULT) {
            result_payload_t *result = (result_payload_t *)result_msg->payload;
            log_info("Result from worker %d: task=%lu, result=%.6f, status=%d",
                     src_rank, result->task_id, result->result, result->status);
            results_received++;
        } else if (result_msg->header.msg_type == MSG_TYPE_HEARTBEAT) {
            log_debug("Heartbeat from worker %d", src_rank);
        }

        message_free(result_msg);
    }

    /* Print statistics */
    transport_stats_t stats;
    transport_get_stats(transport, &stats);
    log_info("Transport statistics:");
    log_info("  Messages sent: %lu", stats.messages_sent);
    log_info("  Messages received: %lu", stats.messages_received);
    log_info("  Bytes sent: %lu", stats.bytes_sent);
    log_info("  Bytes received: %lu", stats.bytes_received);
    log_info("  Errors: %lu", stats.errors);

    /* Shutdown */
    log_info("Coordinator shutting down");
    transport_shutdown(transport);
}

/*
 * Worker example
 */
void run_worker(transport_type_t type, const char *endpoint)
{
    log_info("Starting worker (transport=%s)", transport_type_to_string(type));

    /* Initialize transport */
    transport_config_t config = {
        .type = type,
        .role = TRANSPORT_ROLE_WORKER,
        .endpoint = endpoint,
        .timeout_ms = 10000,
        .max_workers = 0
    };

    transport_t *transport;
    if (transport_init(&transport, &config) != 0) {
        log_error("Failed to initialise transport");
        return;
    }

    int rank = transport_get_rank(transport);
    log_info("Worker rank: %d", rank);

    /* Process tasks */
    log_info("Waiting for tasks...");
    int tasks_processed = 0;

    while (tasks_processed < 10) { /* Process up to 10 tasks for demo */
        message_t *task_msg = transport_recv(transport, NULL);

        if (!task_msg) {
            log_debug("No task received (timeout), sending heartbeat");
            
            /* Send heartbeat */
            message_t *hb = message_alloc(0);
            if (hb) {
                message_set_header(hb, MSG_TYPE_HEARTBEAT, 0);
                transport_send(transport, hb, 0);
                message_free(hb);
            }
            continue;
        }

        if (task_msg->header.msg_type != MSG_TYPE_TASK_SUBMIT) {
            log_warn("Unexpected message type: %s",
                     message_type_to_string(task_msg->header.msg_type));
            message_free(task_msg);
            continue;
        }

        /* Process task */
        task_payload_t *task = (task_payload_t *)task_msg->payload;
        log_info("Processing task %lu (input=%.2f, iterations=%d)",
                 task->task_id, task->input_value, task->iterations);

        /* Simulate computation */
        double result = task->input_value;
        for (int i = 0; i < task->iterations; i++) {
            result = result * 0.999 + 0.001; /* Dummy computation */
        }

        /* Send result back */
        message_t *result_msg = message_alloc(sizeof(result_payload_t));
        if (result_msg) {
            result_payload_t *payload = (result_payload_t *)result_msg->payload;
            payload->task_id = task->task_id;
            payload->result = result;
            payload->status = 0; /* Success */

            message_set_header(result_msg, MSG_TYPE_TASK_RESULT, task->task_id);
            result_msg->header.payload_len = sizeof(result_payload_t);

            if (transport_send(transport, result_msg, 0) == 0) {
                log_info("Result for task %lu sent", task->task_id);
                tasks_processed++;
            } else {
                log_error("Failed to send result for task %lu", task->task_id);
            }

            message_free(result_msg);
        }

        message_free(task_msg);
    }

    /* Print statistics */
    transport_stats_t stats;
    transport_get_stats(transport, &stats);
    log_info("Worker statistics:");
    log_info("  Tasks processed: %d", tasks_processed);
    log_info("  Messages sent: %lu", stats.messages_sent);
    log_info("  Messages received: %lu", stats.messages_received);

    /* Shutdown */
    log_info("Worker shutting down");
    transport_shutdown(transport);
}

int main(int argc, char **argv)
{
    /* Initialize logging */
    if (log_init("transport_example.log", LOG_LEVEL_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialise logging\n");
        return 1;
    }

    error_reporter_install(NULL, "transport_example.log");

    /* Parse arguments */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [coordinator|worker] [transport] [endpoint]\n", argv[0]);
        fprintf(stderr, "  transport: mpi (default) or zmq\n");
        fprintf(stderr, "  endpoint: ZeroMQ endpoint (default: tcp://127.0.0.1:5555)\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  MPI: mpirun -np 4 %s coordinator mpi\n", argv[0]);
        fprintf(stderr, "  ZMQ: %s coordinator zmq tcp://127.0.0.1:5555\n", argv[0]);
        fprintf(stderr, "       %s worker zmq tcp://127.0.0.1:5555\n", argv[0]);
        log_shutdown();
        return 1;
    }

    const char *role_str = argv[1];
    const char *transport_str = argc > 2 ? argv[2] : "mpi";
    const char *endpoint = argc > 3 ? argv[3] : "tcp://127.0.0.1:5555";

    /* Determine transport type */
    transport_type_t type = TRANSPORT_TYPE_MPI;
    if (strcmp(transport_str, "zmq") == 0) {
        type = TRANSPORT_TYPE_ZMQ;
    }

    /* Run coordinator or worker */
    if (strcmp(role_str, "coordinator") == 0) {
        run_coordinator(type, endpoint);
    } else if (strcmp(role_str, "worker") == 0) {
        run_worker(type, endpoint);
    } else {
        log_error("Invalid role: %s (must be 'coordinator' or 'worker')", role_str);
        log_shutdown();
        return 1;
    }

    log_shutdown();
    return 0;
}