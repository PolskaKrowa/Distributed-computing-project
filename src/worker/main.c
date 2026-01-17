/*
 * Distributed Computing Project - Worker
 * 
 * Can operate in two modes:
 * 1. Standalone: Single worker, no MPI
 * 2. Cluster Head: Worker with local MPI cluster for task distribution
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "executor.h"
#include "../net/transport.h"
#include "../runtime/task_queue.h"
#include "../runtime/resource_limits.h"
#include "../storage/metadata.h"
#include "../common/log.h"
#include "../common/errors.h"
#include "../../include/project.h"
#include "../../include/task.h"
#include "../../include/fortran_api.h"
#include "../../include/result.h"
#include "../coordinator/worker_registry.h"

#ifdef HAVE_MPI
#include <mpi.h>
#endif

/* Worker modes */
typedef enum {
    WORKER_MODE_STANDALONE = 0,
    WORKER_MODE_CLUSTER_HEAD = 1
} worker_mode_t;

/* Global state */
static volatile sig_atomic_t running = 1;
static transport_t *global_transport = NULL;
static task_queue_t *local_queue = NULL;
static resource_limits_t *limits = NULL;

#ifdef HAVE_MPI
static MPI_Comm local_comm = MPI_COMM_NULL;
static int mpi_rank = -1;
static int mpi_size = 0;
#endif

/* Signal handler */
static void signal_handler(int sig)
{
    (void)sig;
    log_info("Received shutdown signal");
    running = 0;
}

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* Send registration to coordinator */
static int send_registration(transport_t *trans, uint32_t num_local_ranks,
                             uint32_t max_batch_size, uint32_t capabilities)
{
    /* Prepare registration payload */
    uint32_t payload[3] = {
        num_local_ranks,
        max_batch_size,
        capabilities
    };
    
    message_t *msg = message_alloc(sizeof(payload));
    if (!msg) return -1;
    
    memcpy(msg->payload, payload, sizeof(payload));
    message_set_header(msg, MSG_TYPE_WORKER_REGISTER, 0);
    msg->header.payload_len = sizeof(payload);
    
    int rc = transport_send(trans, msg, 0);
    message_free(msg);
    
    if (rc == 0) {
        log_info("Sent registration (local_ranks=%u, batch_size=%u, caps=0x%x)",
                 num_local_ranks, max_batch_size, capabilities);
    }
    
    return rc;
}

/* Send heartbeat to coordinator */
static int send_heartbeat(transport_t *trans)
{
    message_t *msg = message_alloc(0);
    if (!msg) return -1;
    
    message_set_header(msg, MSG_TYPE_HEARTBEAT, 0);
    msg->header.payload_len = 0;
    
    int rc = transport_send(trans, msg, 0);
    message_free(msg);
    
    return rc;
}

/* Send task result to coordinator */
static int send_result(transport_t *trans, const task_result_t *result)
{
    message_t *msg = message_alloc(sizeof(task_result_t));
    if (!msg) return -1;
    
    memcpy(msg->payload, result, sizeof(task_result_t));
    message_set_header(msg, MSG_TYPE_TASK_RESULT, result->task_id);
    msg->header.payload_len = sizeof(task_result_t);
    
    int rc = transport_send(trans, msg, 0);
    message_free(msg);
    
    return rc;
}

#ifdef HAVE_MPI
/* MPI worker process (ranks 1+) */
static int run_mpi_worker(void)
{
    log_info("MPI worker %d/%d started", mpi_rank, mpi_size);
    
    while (running) {
        /* Receive task from cluster head (rank 0) */
        task_t task;
        MPI_Status status;
        
        int flag;
        MPI_Iprobe(0, MPI_ANY_TAG, local_comm, &flag, &status);
        
        if (!flag) {
            usleep(10000); /* 10ms */
            continue;
        }
        
        if (status.MPI_TAG == 999) {
            /* Shutdown signal */
            log_info("MPI worker %d received shutdown", mpi_rank);
            break;
        }
        
        MPI_Recv(&task, sizeof(task_t), MPI_BYTE, 0, MPI_ANY_TAG, 
                local_comm, &status);
        
        log_debug("MPI worker %d received task %lu", mpi_rank, task.task_id);
        
        /* Execute task */
        task_result_t result = {0};
        result.task_id = task.task_id;
        result.worker_id = mpi_rank;
        
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        /* Call Fortran kernel (placeholder - would actually execute) */
        result.status = 0; /* OK for now */
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        result.exec_time_us = (end.tv_sec - start.tv_sec) * 1000000 +
                             (end.tv_nsec - start.tv_nsec) / 1000;
        
        /* Send result back to head */
        MPI_Send(&result, sizeof(task_result_t), MPI_BYTE, 0, 0, local_comm);
        
        log_debug("MPI worker %d completed task %lu", mpi_rank, task.task_id);
    }
    
    log_info("MPI worker %d shutting down", mpi_rank);
    return 0;
}

/* Distribute task to local MPI worker */
static int distribute_to_mpi_worker(int worker_rank, const task_t *task)
{
    MPI_Send(task, sizeof(task_t), MPI_BYTE, worker_rank, 0, local_comm);
    log_debug("Distributed task %lu to MPI worker %d", task->task_id, worker_rank);
    return 0;
}

/* Collect result from local MPI worker */
static int collect_from_mpi_worker(task_result_t *result)
{
    MPI_Status status;
    int flag;
    
    /* Non-blocking check */
    MPI_Iprobe(MPI_ANY_SOURCE, 0, local_comm, &flag, &status);
    if (!flag) return -1;
    
    MPI_Recv(result, sizeof(task_result_t), MPI_BYTE, 
             status.MPI_SOURCE, 0, local_comm, &status);
    
    log_debug("Collected result for task %lu from MPI worker %d",
             result->task_id, status.MPI_SOURCE);
    
    return 0;
}

/* Shutdown local MPI workers */
static void shutdown_mpi_workers(void)
{
    log_info("Shutting down %d local MPI workers", mpi_size - 1);
    
    for (int i = 1; i < mpi_size; i++) {
        /* Send shutdown signal (tag 999) */
        int dummy = 0;
        MPI_Send(&dummy, 1, MPI_INT, i, 999, local_comm);
    }
}
#endif

/* Standalone execution (no MPI) */
static int execute_task_standalone(const task_t *task, task_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->task_id = task->task_id;
    result->worker_id = 0;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    /* Execute Fortran kernel */
    /* In real implementation, would call fortran_model_run_v1() */
    /* For now, simulate execution */
    usleep(100000); /* 100ms */
    result->status = 0; /* OK */
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    result->exec_time_us = (end.tv_sec - start.tv_sec) * 1000000 +
                          (end.tv_nsec - start.tv_nsec) / 1000;
    
    log_debug("Executed task %lu standalone (exec_time=%lu us)",
             task->task_id, result->exec_time_us);
    
    return 0;
}

/* Main worker loop (cluster head or standalone) */
static int run_worker(const char *coordinator_endpoint, worker_mode_t mode)
{
    /* Initialize global transport to coordinator */
    transport_config_t config = {
        .type = TRANSPORT_TYPE_ZMQ,
        .role = TRANSPORT_ROLE_WORKER,
        .endpoint = coordinator_endpoint,
        .timeout_ms = 1000,
        .max_workers = 0
    };
    
    if (transport_init(&global_transport, &config) != 0) {
        log_fatal("Failed to initialize global transport");
        return 1;
    }
    
    /* Create local task queue */
    task_queue_config_t queue_config = {
        .max_size = 1000,
        .allow_duplicates = 0
    };
    local_queue = task_queue_create(&queue_config);
    if (!local_queue) {
        log_fatal("Failed to create local queue");
        return 1;
    }
    
    /* Create resource limiter */
    resource_limits_config_t limit_config = {
        .max_memory_bytes = 4ULL * 1024 * 1024 * 1024,  /* 4 GB */
        .max_cpu_percent = 90.0,
        .max_concurrent_tasks = 8,
        .enforce_hard_limits = 1
    };
    limits = resource_limits_create(&limit_config);
    
    /* Determine capabilities */
    uint32_t num_local_ranks = 1;
    uint32_t max_batch_size = 10;
    uint32_t capabilities = 0;
    
#ifdef HAVE_MPI
    if (mode == WORKER_MODE_CLUSTER_HEAD) {
        num_local_ranks = mpi_size;
        max_batch_size = mpi_size * 5; /* 5 tasks per local worker */
        capabilities |= WORKER_CAP_MPI_CLUSTER;
    }
#endif
    
    /* Register with coordinator */
    send_registration(global_transport, num_local_ranks, 
                     max_batch_size, capabilities);
    
    log_info("Worker started (mode=%s, local_ranks=%u, batch_size=%u)",
             mode == WORKER_MODE_CLUSTER_HEAD ? "cluster_head" : "standalone",
             num_local_ranks, max_batch_size);
    
    time_t last_heartbeat = time(NULL);
    
    /* Main loop */
    while (running) {
        /* Send periodic heartbeat */
        time_t now = time(NULL);
        if (now - last_heartbeat >= 10) {
            send_heartbeat(global_transport);
            last_heartbeat = now;
        }
        
        /* Check for incoming task batches from coordinator */
        int src;
        message_t *msg = transport_recv(global_transport, &src);
        
        if (msg && msg->header.msg_type == MSG_TYPE_TASK_SUBMIT) {
            /* Unpack task batch */
            if (msg->header.payload_len >= sizeof(uint32_t)) {
                uint32_t batch_count = *(uint32_t *)msg->payload;
                task_t *batch = (task_t *)((char *)msg->payload + sizeof(uint32_t));
                
                log_info("Received batch of %u tasks from coordinator", batch_count);
                
                /* Add tasks to local queue */
                for (uint32_t i = 0; i < batch_count; i++) {
                    task_queue_enqueue(local_queue, &batch[i]);
                }
            }
            message_free(msg);
        } else if (msg) {
            message_free(msg);
        }
        
        /* Process tasks from local queue */
        task_t task;
        if (task_queue_dequeue(local_queue, &task) == 0) {
            /* Check resources */
            if (resource_limits_reserve_task(limits) != 0) {
                /* Resources unavailable - re-queue */
                task_queue_enqueue(local_queue, &task);
                usleep(100000); /* 100ms */
                continue;
            }
            
            task_result_t result;
            
#ifdef HAVE_MPI
            if (mode == WORKER_MODE_CLUSTER_HEAD) {
                /* Find idle MPI worker */
                static int next_worker = 1;
                distribute_to_mpi_worker(next_worker, &task);
                next_worker = (next_worker % (mpi_size - 1)) + 1;
                
                /* Don't block - results collected separately */
                resource_limits_release_task(limits, 1);
                continue;
            }
#endif
            
            /* Standalone execution */
            execute_task_standalone(&task, &result);
            
            /* Send result to coordinator */
            send_result(global_transport, &result);
            
            resource_limits_release_task(limits, result.status == 0);
        }
        
#ifdef HAVE_MPI
        /* Collect results from MPI workers */
        if (mode == WORKER_MODE_CLUSTER_HEAD) {
            task_result_t result;
            while (collect_from_mpi_worker(&result) == 0) {
                send_result(global_transport, &result);
            }
        }
#endif
        
        /* Update resource stats */
        resource_limits_update(limits);
        
        /* Small sleep to avoid busy-waiting */
        usleep(1000); /* 1ms */
    }
    
#ifdef HAVE_MPI
    if (mode == WORKER_MODE_CLUSTER_HEAD) {
        shutdown_mpi_workers();
    }
#endif
    
    log_info("Worker shutting down");
    
    /* Cleanup */
    if (limits) resource_limits_destroy(limits);
    task_queue_destroy(local_queue);
    transport_shutdown(global_transport);
    
    return 0;
}

int main(int argc, char *argv[])
{
    int rc = 0;
    worker_mode_t mode = WORKER_MODE_STANDALONE;
    
#ifdef HAVE_MPI
    /* Initialize MPI if available */
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
    
    /* Determine mode */
    if (mpi_size > 1) {
        mode = WORKER_MODE_CLUSTER_HEAD;
        local_comm = MPI_COMM_WORLD;
    }
    
    /* If not rank 0, become MPI worker */
    if (mpi_rank > 0) {
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "worker_mpi_%d.log", mpi_rank);
        log_init(log_path, LOG_LEVEL_DEBUG);
        error_reporter_install(NULL, log_path);
        setup_signals();
        
        rc = run_mpi_worker();
        
        log_shutdown();
        MPI_Finalize();
        return rc;
    }
#endif
    
    /* Rank 0 or standalone: initialize as main worker */
    log_init("worker.log", LOG_LEVEL_DEBUG);
    error_reporter_install(NULL, "worker.log");
    setup_signals();
    
    /* Parse arguments */
    const char *coordinator_endpoint = "tcp://127.0.0.1:5555";
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--coordinator") == 0 && i + 1 < argc) {
            coordinator_endpoint = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --coordinator <url>  Coordinator endpoint (default: tcp://127.0.0.1:5555)\n");
            printf("  --help               Show this help\n");
#ifdef HAVE_MPI
            printf("\nMPI Mode:\n");
            printf("  Run with mpirun to enable cluster head mode\n");
            printf("  Example: mpirun -np 4 %s --coordinator tcp://coord:5555\n", argv[0]);
#endif
            return 0;
        }
    }
    
#ifdef HAVE_MPI
    log_info("Worker initializing (MPI rank %d/%d, mode=%s)",
             mpi_rank, mpi_size,
             mode == WORKER_MODE_CLUSTER_HEAD ? "cluster_head" : "standalone");
#else
    log_info("Worker initializing (standalone mode, MPI not available)");
#endif
    
    rc = run_worker(coordinator_endpoint, mode);
    
    log_shutdown();
    
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    
    return rc;
}