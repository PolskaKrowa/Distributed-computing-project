/*
 * Distributed Computing Project - Coordinator
 * 
 * Manages global task distribution and worker orchestration.
 * Supports batching tasks for remote workers with limited connectivity.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "scheduler.h"
#include "worker_registry.h"
#include "../net/transport.h"
#include "../runtime/task_queue.h"
#include "../storage/metadata.h"
#include "../common/log.h"
#include "../common/errors.h"
#include "../../include/project.h"
#include "../../include/task.h"
#include "../../include/result.h"

/* Global state */
static volatile sig_atomic_t running = 1;
static transport_t *transport = NULL;
static scheduler_t *scheduler = NULL;
static task_queue_t *task_queue = NULL;
static metadata_store_t *meta_store = NULL;

/* Signal handler for graceful shutdown */
static void signal_handler(int sig)
{
    (void)sig;
    log_info("Received shutdown signal");
    running = 0;
}

/* Install signal handlers */
static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* Handle worker registration */
static void handle_worker_register(scheduler_t *sched, int worker_id, message_t *msg)
{
    /* Extract worker capabilities from payload */
    worker_info_t info = {0};
    info.worker_id = worker_id;
    info.last_heartbeat = time(NULL);
    info.state = WORKER_STATE_IDLE;
    
    /* Parse capabilities from message payload */
    if (msg->header.payload_len > 0 && msg->payload) {
        /* Payload format: [num_local_ranks:4][max_batch_size:4][capabilities_flags:4] */
        if (msg->header.payload_len >= 12) {
            uint32_t *data = (uint32_t *)msg->payload;
            info.num_local_ranks = data[0];
            info.max_batch_size = data[1];
            info.capabilities = data[2];
        }
    }
    
    /* Default values if not provided */
    if (info.num_local_ranks == 0) info.num_local_ranks = 1;
    if (info.max_batch_size == 0) info.max_batch_size = 10;
    
    scheduler_register_worker(sched, &info);
    
    log_info("Worker %d registered (local_ranks=%u, max_batch=%u, capabilities=0x%x)",
             worker_id, info.num_local_ranks, info.max_batch_size, info.capabilities);
}

/* Handle worker heartbeat */
static void handle_heartbeat(scheduler_t *sched, int worker_id)
{
    scheduler_update_heartbeat(sched, worker_id);
    log_debug("Heartbeat from worker %d", worker_id);
}

/* Handle task result */
static void handle_task_result(scheduler_t *sched, metadata_store_t *meta, 
                               int worker_id, message_t *msg)
{
    if (!msg->payload || msg->header.payload_len < sizeof(task_result_t)) {
        log_error("Invalid task result from worker %d", worker_id);
        return;
    }
    
    task_result_t *result = (task_result_t *)msg->payload;
    
    /* Mark task as completed */
    scheduler_mark_task_complete(sched, result->task_id, result->status);
    
    /* Store metadata */
    if (meta) {
        metadata_store_task_info(meta,
                                result->task_id,
                                0, /* model_id - extract from task */
                                result->status,
                                result->exec_time_us,
                                result->queue_time_us,
                                worker_id);
    }
    
    log_info("Task %lu completed by worker %d (status=%d, exec_time=%lu us)",
             result->task_id, worker_id, result->status, result->exec_time_us);
}

/* Create a batch of tasks for a worker */
static int create_task_batch(task_queue_t *queue, task_t *batch, 
                             size_t max_batch_size, size_t *batch_count)
{
    *batch_count = 0;
    
    /* Try to dequeue up to max_batch_size tasks */
    for (size_t i = 0; i < max_batch_size; i++) {
        if (task_queue_dequeue(queue, &batch[i]) != 0) {
            break; /* No more tasks */
        }
        (*batch_count)++;
    }
    
    return (*batch_count > 0) ? 0 : -1;
}

/* Send task batch to worker */
static int send_task_batch(transport_t *trans, int worker_id, 
                           const task_t *batch, size_t batch_count)
{
    /* Calculate payload size */
    size_t payload_size = sizeof(uint32_t) + batch_count * sizeof(task_t);
    
    message_t *msg = message_alloc(payload_size);
    if (!msg) {
        log_error("Failed to allocate message for task batch");
        return -1;
    }
    
    /* Pack batch: [count:4][task_1][task_2]...[task_n] */
    uint32_t *count_ptr = (uint32_t *)msg->payload;
    *count_ptr = batch_count;
    
    memcpy((char *)msg->payload + sizeof(uint32_t), 
           batch, 
           batch_count * sizeof(task_t));
    
    message_set_header(msg, MSG_TYPE_TASK_SUBMIT, batch[0].task_id);
    msg->header.payload_len = payload_size;
    
    int rc = transport_send(trans, msg, worker_id);
    message_free(msg);
    
    if (rc == 0) {
        log_info("Sent batch of %zu tasks to worker %d", batch_count, worker_id);
    } else {
        log_error("Failed to send task batch to worker %d", worker_id);
    }
    
    return rc;
}

/* Main coordinator loop */
static int run_coordinator(const char *endpoint)
{
    /* Initialize transport */
    transport_config_t config = {
        .type = TRANSPORT_TYPE_ZMQ,
        .role = TRANSPORT_ROLE_COORDINATOR,
        .endpoint = endpoint,
        .timeout_ms = 1000,
        .max_workers = 256
    };
    
    if (transport_init(&transport, &config) != 0) {
        log_fatal("Failed to initialize transport");
        return 1;
    }
    
    /* Create task queue */
    task_queue_config_t queue_config = {
        .max_size = 10000,
        .allow_duplicates = 0
    };
    task_queue = task_queue_create(&queue_config);
    if (!task_queue) {
        log_fatal("Failed to create task queue");
        return 1;
    }
    
    /* Create scheduler */
    scheduler_config_t sched_config = {
        .max_workers = 256,
        .heartbeat_timeout_secs = 30,
        .scheduling_policy = SCHED_POLICY_LOAD_BALANCED
    };
    scheduler = scheduler_create(&sched_config);
    if (!scheduler) {
        log_fatal("Failed to create scheduler");
        return 1;
    }
    
    /* Open metadata store */
    meta_store = metadata_store_open("./metadata", 1);
    if (!meta_store) {
        log_warn("Failed to open metadata store - continuing without metadata");
    }
    
    log_info("Coordinator started on %s", endpoint);
    
    /* Main event loop */
    while (running) {
        /* Check for incoming messages */
        int src_worker;
        message_t *msg = transport_recv(transport, &src_worker);
        
        if (msg) {
            switch (msg->header.msg_type) {
                case MSG_TYPE_WORKER_REGISTER:
                    handle_worker_register(scheduler, src_worker, msg);
                    break;
                    
                case MSG_TYPE_HEARTBEAT:
                    handle_heartbeat(scheduler, src_worker);
                    break;
                    
                case MSG_TYPE_TASK_RESULT:
                    handle_task_result(scheduler, meta_store, src_worker, msg);
                    break;
                    
                default:
                    log_warn("Unknown message type %d from worker %d",
                            msg->header.msg_type, src_worker);
            }
            
            message_free(msg);
        }
        
        /* Schedule tasks to workers */
        worker_info_t workers[256];
        int worker_count = scheduler_get_idle_workers(scheduler, workers, 256);
        
        for (int i = 0; i < worker_count && !task_queue_is_empty(task_queue); i++) {
            /* Create batch for this worker */
            task_t batch[100];
            size_t batch_count;
            
            size_t max_batch = (workers[i].max_batch_size > 0) ? 
                              workers[i].max_batch_size : 10;
            
            if (create_task_batch(task_queue, batch, max_batch, &batch_count) == 0) {
                /* Send batch to worker */
                if (send_task_batch(transport, workers[i].worker_id, 
                                   batch, batch_count) == 0) {
                    /* Mark worker as busy */
                    scheduler_mark_worker_busy(scheduler, workers[i].worker_id, 
                                              batch_count);
                    
                    /* Track tasks */
                    for (size_t j = 0; j < batch_count; j++) {
                        scheduler_track_task(scheduler, batch[j].task_id, 
                                            workers[i].worker_id);
                    }
                } else {
                    /* Failed to send - re-queue tasks */
                    for (size_t j = 0; j < batch_count; j++) {
                        task_queue_enqueue(task_queue, &batch[j]);
                    }
                }
            }
        }
        
        /* Check for dead workers */
        scheduler_check_timeouts(scheduler);
        
        /* Small sleep to avoid busy-waiting */
        usleep(10000); /* 10ms */
    }
    
    log_info("Coordinator shutting down");
    
    /* Cleanup */
    if (meta_store) metadata_store_close(meta_store);
    scheduler_destroy(scheduler);
    task_queue_destroy(task_queue);
    transport_shutdown(transport);
    
    return 0;
}

/* Load tasks from file (example) */
static int load_tasks_from_file(const char *filename, task_queue_t *queue)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        log_error("Failed to open task file: %s", filename);
        return -1;
    }
    
    int count = 0;
    char line[1024];
    
    while (fgets(line, sizeof(line), f)) {
        /* Parse task from line: task_id model_id input_size output_size */
        uint64_t task_id;
        uint32_t model_id;
        size_t input_size, output_size;
        
        if (sscanf(line, "%lu %u %zu %zu", 
                  &task_id, &model_id, &input_size, &output_size) == 4) {
            
            task_t task = {
                .task_id = task_id,
                .model_id = model_id,
                .input = NULL, /* Would allocate and populate in real system */
                .input_size = input_size,
                .output = NULL,
                .output_size = output_size,
                .api_version = API_VERSION_CURRENT,
                .trace_id = task_id,
                .timeout_secs = 300,
                .flags = 0
            };
            
            if (task_queue_enqueue(queue, &task) == 0) {
                count++;
            }
        }
    }
    
    fclose(f);
    log_info("Loaded %d tasks from %s", count, filename);
    return count;
}

int main(int argc, char *argv[])
{
    /* Initialize logging */
    if (log_init("coordinator.log", LOG_LEVEL_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return 1;
    }
    
    /* Install error reporter */
    error_reporter_install(NULL, "coordinator.log");
    
    /* Setup signal handlers */
    setup_signals();
    
    /* Parse command line */
    const char *endpoint = "tcp://0.0.0.0:5555";
    const char *task_file = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--endpoint") == 0 && i + 1 < argc) {
            endpoint = argv[++i];
        } else if (strcmp(argv[i], "--tasks") == 0 && i + 1 < argc) {
            task_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --endpoint <url>   ZeroMQ endpoint (default: tcp://0.0.0.0:5555)\n");
            printf("  --tasks <file>     Load tasks from file\n");
            printf("  --help             Show this help\n");
            return 0;
        }
    }
    
    log_info("Coordinator starting (endpoint=%s)", endpoint);
    
    int rc = run_coordinator(endpoint);
    
    /* Load tasks if provided */
    if (task_file && task_queue) {
        load_tasks_from_file(task_file, task_queue);
    }
    
    log_shutdown();
    return rc;
}