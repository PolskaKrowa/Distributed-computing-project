#include "worker.h"
#include "../common/log.h"
#include "../../include/project.h"
#include "../common/time_utils.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

/* Sub-worker tracking (for master-worker mode) */
typedef struct sub_worker {
    int rank;
    int busy;
    uint64_t current_task_id;
    uint64_t tasks_completed;
} sub_worker_t;

/* Worker structure */
struct worker {
    /* Configuration */
    worker_config_t config;
    
    /* Transport layer */
    transport_t *transport;         /* To coordinator */
    transport_t *local_transport;   /* To sub-workers (master mode) */
    
    /* Resource management */
    resource_limits_t *resources;
    
    /* Sub-workers (master mode only) */
    sub_worker_t *sub_workers;
    int num_sub_workers;
    
    /* Current task */
    task_t current_task;
    int task_active;
    uint64_t task_start_time_us;
    
    /* Statistics */
    worker_stats_t stats;
    
    /* Control */
    int running;
    int registered;
    
    /* Thread synchronization */
    pthread_mutex_t mutex;
    pthread_t heartbeat_thread;
};

/* Helper: get current time in microseconds */
static uint64_t current_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* Helper: allocate buffers for task execution */
static int allocate_task_buffers(const task_t *task,
                                 void **input_buf,
                                 void **output_buf)
{
    *input_buf = NULL;
    *output_buf = NULL;
    
    if (task->input_size > 0) {
        *input_buf = malloc(task->input_size);
        if (!*input_buf) {
            log_error("Failed to allocate input buffer (%zu bytes)", task->input_size);
            return -1;
        }
        
        if (task->input) {
            memcpy(*input_buf, task->input, task->input_size);
        }
    }
    
    if (task->output_size > 0) {
        *output_buf = malloc(task->output_size);
        if (!*output_buf) {
            log_error("Failed to allocate output buffer (%zu bytes)", task->output_size);
            free(*input_buf);
            return -1;
        }
        memset(*output_buf, 0, task->output_size);
    }
    
    return 0;
}

/* Execute task using Fortran API */
static int execute_fortran_task(worker_t *worker,
                                const task_t *task,
                                task_result_t *result)
{
    log_info("Executing task %lu (model_id=%u, input=%zu bytes, output=%zu bytes)",
             task->task_id, task->model_id, task->input_size, task->output_size);
    
    /* Allocate buffers */
    void *input_buf = NULL;
    void *output_buf = NULL;
    
    if (allocate_task_buffers(task, &input_buf, &output_buf) != 0) {
        result->status = ERR_BUFFER_TOO_SMALL;
        return -1;
    }
    
    /* Record start time */
    uint64_t start_time = current_time_us();
    
    /* Call Fortran kernel */
    int32_t fortran_status = 0;
    int32_t rc = fortran_model_run_v1(
        input_buf,
        task->input_size,
        output_buf,
        task->output_size,
        task->meta,
        task->meta_size,
        task->trace_id,
        &fortran_status
    );
    
    uint64_t end_time = current_time_us();
    uint64_t exec_time = end_time - start_time;
    
    /* Fill result structure */
    result->task_id = task->task_id;
    result->status = (rc == 0) ? OK : ERR_COMPUTATION_FAILED;
    result->exec_time_us = exec_time;
    result->output = output_buf;
    result->output_bytes_written = (rc == 0) ? task->output_size : 0;
    
    log_info("Task %lu completed: status=%s exec_time=%lu us fortran_status=%d",
             task->task_id,
             status_to_string(result->status),
             exec_time,
             fortran_status);
    
    /* Update worker statistics */
    pthread_mutex_lock(&worker->mutex);
    if (rc == 0) {
        worker->stats.tasks_completed++;
    } else {
        worker->stats.tasks_failed++;
    }
    worker->stats.total_exec_time_us += exec_time;
    pthread_mutex_unlock(&worker->mutex);
    
    /* Clean up input buffer (output is kept in result) */
    free(input_buf);
    
    return (rc == 0) ? 0 : -1;
}

/* Master-worker: dispatch task to sub-worker */
static int dispatch_to_sub_worker(worker_t *worker, const task_t *task)
{
    if (worker->config.mode != WORKER_MODE_MASTER) {
        return -1;
    }
    
    pthread_mutex_lock(&worker->mutex);
    
    /* Find available sub-worker */
    sub_worker_t *available = NULL;
    for (int i = 0; i < worker->num_sub_workers; ++i) {
        if (!worker->sub_workers[i].busy) {
            available = &worker->sub_workers[i];
            break;
        }
    }
    
    if (!available) {
        pthread_mutex_unlock(&worker->mutex);
        return -1;  /* No workers available */
    }
    
    /* Send task to sub-worker */
    message_t *msg = message_alloc(sizeof(task_t));
    if (!msg) {
        pthread_mutex_unlock(&worker->mutex);
        return -1;
    }
    
    message_set_header(msg, MSG_TYPE_TASK_SUBMIT, task->task_id);
    memcpy(msg->payload, task, sizeof(task_t));
    msg->header.payload_len = sizeof(task_t);
    
    int rc = transport_send(worker->local_transport, msg, available->rank);
    message_free(msg);
    
    if (rc == 0) {
        available->busy = 1;
        available->current_task_id = task->task_id;
        log_debug("Dispatched task %lu to sub-worker rank %d",
                 task->task_id, available->rank);
    }
    
    pthread_mutex_unlock(&worker->mutex);
    return rc;
}

/* Master-worker: collect result from sub-worker */
static int collect_from_sub_worker(worker_t *worker, task_result_t *result)
{
    if (worker->config.mode != WORKER_MODE_MASTER) {
        return -1;
    }
    
    /* Poll for result from any sub-worker */
    int poll_rc = transport_poll(worker->local_transport, 100);
    if (poll_rc <= 0) {
        return -1;  /* No message available */
    }
    
    int src_rank = -1;
    message_t *msg = transport_recv(worker->local_transport, &src_rank);
    if (!msg) {
        return -1;
    }
    
    if (msg->header.msg_type == MSG_TYPE_TASK_RESULT) {
        if (msg->header.payload_len >= sizeof(task_result_t)) {
            memcpy(result, msg->payload, sizeof(task_result_t));
            
            /* Mark sub-worker as available */
            pthread_mutex_lock(&worker->mutex);
            for (int i = 0; i < worker->num_sub_workers; ++i) {
                if (worker->sub_workers[i].rank == src_rank) {
                    worker->sub_workers[i].busy = 0;
                    worker->sub_workers[i].tasks_completed++;
                    break;
                }
            }
            pthread_mutex_unlock(&worker->mutex);
            
            log_debug("Collected result for task %lu from sub-worker rank %d",
                     result->task_id, src_rank);
            
            message_free(msg);
            return 0;
        }
    }
    
    message_free(msg);
    return -1;
}

/* Heartbeat thread */
static void *heartbeat_thread_func(void *arg)
{
    worker_t *worker = (worker_t *)arg;
    
    log_info("Heartbeat thread started (interval=%u ms)",
             worker->config.heartbeat_interval_ms);
    
    while (worker->running) {
        usleep(worker->config.heartbeat_interval_ms * 1000);
        
        /* Send heartbeat */
        message_t *msg = message_alloc(0);
        if (msg) {
            message_set_header(msg, MSG_TYPE_HEARTBEAT, 0);
            transport_send(worker->transport, msg, 0);
            message_free(msg);
        }
        
        /* Update resource statistics */
        if (worker->resources) {
            resource_limits_update(worker->resources);
            
            limit_status_t status = resource_limits_check_all(worker->resources);
            if (status == LIMIT_EXCEEDED || status == LIMIT_CRITICAL) {
                log_warn("Resource limits exceeded: %s",
                        resource_limit_status_string(status));
            }
        }
    }
    
    log_info("Heartbeat thread stopped");
    return NULL;
}

worker_t *worker_create(const worker_config_t *config)
{
    if (!config) {
        log_error("worker_create: NULL config");
        return NULL;
    }
    
    worker_t *worker = calloc(1, sizeof(*worker));
    if (!worker) {
        log_error("Failed to allocate worker");
        return NULL;
    }
    
    memcpy(&worker->config, config, sizeof(worker_config_t));
    
    /* Create resource limiter */
    worker->resources = resource_limits_create(&config->resource_config);
    if (!worker->resources) {
        log_warn("Failed to create resource limiter (continuing without it)");
    }
    
    /* Initialise transport to coordinator */
    transport_config_t transport_cfg = {
        .type = config->transport_type,
        .role = TRANSPORT_ROLE_WORKER,
        .endpoint = config->coordinator_endpoint,
        .timeout_ms = 1000,
        .max_workers = 0
    };
    
    if (transport_init(&worker->transport, &transport_cfg) != 0) {
        log_error("Failed to initialise transport to coordinator");
        if (worker->resources) resource_limits_destroy(worker->resources);
        free(worker);
        return NULL;
    }
    
    /* Initialise sub-workers for master mode */
    if (config->mode == WORKER_MODE_MASTER && config->num_sub_workers > 0) {
        worker->num_sub_workers = config->num_sub_workers;
        worker->sub_workers = calloc(worker->num_sub_workers, sizeof(sub_worker_t));
        
        if (!worker->sub_workers) {
            log_error("Failed to allocate sub-worker array");
            transport_shutdown(worker->transport);
            if (worker->resources) resource_limits_destroy(worker->resources);
            free(worker);
            return NULL;
        }
        
        /* Initialise sub-worker entries */
        for (int i = 0; i < worker->num_sub_workers; ++i) {
            worker->sub_workers[i].rank = i + 1;  /* Rank 0 is master */
            worker->sub_workers[i].busy = 0;
        }
        
        /* Use provided local transport or create MPI transport */
        if (config->local_transport) {
            worker->local_transport = config->local_transport;
        } else {
            /* Would initialise MPI transport here */
            log_warn("Master worker: no local transport provided");
        }
        
        log_info("Created master worker with %d sub-workers", worker->num_sub_workers);
    }
    
    /* Create working directory if needed */
    if (config->work_dir) {
        struct stat st;
        if (stat(config->work_dir, &st) != 0) {
            if (mkdir(config->work_dir, 0755) != 0) {
                log_error("Failed to create work directory: %s", config->work_dir);
            }
        }
    }
    
    pthread_mutex_init(&worker->mutex, NULL);
    
    log_info("Created worker (mode=%s, transport=%s)",
             config->mode == WORKER_MODE_MASTER ? "master" :
             config->mode == WORKER_MODE_LOCAL_MPI ? "local" : "standalone",
             transport_type_to_string(config->transport_type));
    
    return worker;
}

void worker_destroy(worker_t *worker)
{
    if (!worker) return;
    
    log_info("Destroying worker");
    
    worker->running = 0;
    
    if (worker->heartbeat_thread) {
        pthread_join(worker->heartbeat_thread, NULL);
    }
    
    /* Send shutdown message */
    if (worker->registered) {
        message_t *msg = message_alloc(0);
        if (msg) {
            message_set_header(msg, MSG_TYPE_WORKER_SHUTDOWN, 0);
            transport_send(worker->transport, msg, 0);
            message_free(msg);
        }
    }
    
    if (worker->transport) transport_shutdown(worker->transport);
    if (worker->resources) resource_limits_destroy(worker->resources);
    
    free(worker->sub_workers);
    pthread_mutex_destroy(&worker->mutex);
    
    free(worker);
    
    log_info("Worker destroyed");
}

int worker_register(worker_t *worker)
{
    if (!worker) return -1;
    
    message_t *msg = message_alloc(0);
    if (!msg) {
        log_error("Failed to allocate registration message");
        return -1;
    }
    
    message_set_header(msg, MSG_TYPE_WORKER_REGISTER, 0);
    
    int rc = transport_send(worker->transport, msg, 0);
    message_free(msg);
    
    if (rc == 0) {
        worker->registered = 1;
        log_info("Registered with coordinator");
    } else {
        log_error("Failed to register with coordinator");
    }
    
    return rc;
}

int worker_send_heartbeat(worker_t *worker)
{
    if (!worker) return -1;
    
    message_t *msg = message_alloc(0);
    if (!msg) return -1;
    
    message_set_header(msg, MSG_TYPE_HEARTBEAT, 0);
    int rc = transport_send(worker->transport, msg, 0);
    message_free(msg);
    
    return rc;
}

int worker_execute_task(worker_t *worker, const task_t *task, task_result_t *result)
{
    if (!worker || !task || !result) return -1;
    
    memset(result, 0, sizeof(*result));
    result->task_id = task->task_id;
    
    /* Check resource availability */
    if (worker->resources) {
        if (resource_limits_reserve_task(worker->resources) != 0) {
            log_warn("Cannot accept task %lu - resource limits exceeded", task->task_id);
            result->status = ERR_TIMEOUT;
            return -1;
        }
    }
    
    pthread_mutex_lock(&worker->mutex);
    worker->task_active = 1;
    memcpy(&worker->current_task, task, sizeof(task_t));
    worker->task_start_time_us = current_time_us();
    pthread_mutex_unlock(&worker->mutex);
    
    /* Execute based on worker mode */
    int rc;
    
    if (worker->config.mode == WORKER_MODE_MASTER) {
        /* Dispatch to sub-worker */
        rc = dispatch_to_sub_worker(worker, task);
        if (rc == 0) {
            /* Wait for result */
            while (collect_from_sub_worker(worker, result) != 0) {
                if (!worker->running) {
                    rc = -1;
                    break;
                }
                usleep(10000);  /* 10ms */
            }
        }
    } else {
        /* Execute locally */
        rc = execute_fortran_task(worker, task, result);
    }
    
    pthread_mutex_lock(&worker->mutex);
    worker->task_active = 0;
    worker->stats.tasks_executed++;
    pthread_mutex_unlock(&worker->mutex);
    
    /* Release resources */
    if (worker->resources) {
        resource_limits_release_task(worker->resources, rc == 0);
    }
    
    return rc;
}

int worker_run(worker_t *worker)
{
    if (!worker) return -1;
    
    /* Register with coordinator */
    if (worker_register(worker) != 0) {
        log_error("Failed to register with coordinator");
        return -1;
    }
    
    worker->running = 1;
    
    /* Start heartbeat thread */
    pthread_create(&worker->heartbeat_thread, NULL, heartbeat_thread_func, worker);
    
    log_info("Worker running and waiting for tasks");
    
    /* Main task processing loop */
    while (worker->running) {
        /* Poll for incoming tasks */
        int poll_rc = transport_poll(worker->transport, 100);
        
        if (poll_rc <= 0) {
            continue;  /* No message or timeout */
        }
        
        message_t *msg = transport_recv(worker->transport, NULL);
        if (!msg) {
            continue;
        }
        
        if (msg->header.msg_type == MSG_TYPE_TASK_SUBMIT) {
            if (msg->header.payload_len < sizeof(task_t)) {
                log_error("Invalid task message size");
                message_free(msg);
                continue;
            }
            
            task_t *task = (task_t *)msg->payload;
            task_result_t result;
            
            log_info("Received task %lu from coordinator", task->task_id);
            
            /* Execute task */
            int exec_rc = worker_execute_task(worker, task, &result);
            
            /* Send result back */
            message_t *result_msg = message_alloc(sizeof(task_result_t));
            if (result_msg) {
                message_set_header(result_msg, MSG_TYPE_TASK_RESULT, task->task_id);
                memcpy(result_msg->payload, &result, sizeof(task_result_t));
                result_msg->header.payload_len = sizeof(task_result_t);
                
                transport_send(worker->transport, result_msg, 0);
                message_free(result_msg);
            }
            
            /* Free output buffer from result */
            if (result.output) {
                free(result.output);
            }
            
        } else if (msg->header.msg_type == MSG_TYPE_WORKER_SHUTDOWN) {
            log_info("Received shutdown command from coordinator");
            worker->running = 0;
        }
        
        message_free(msg);
    }
    
    log_info("Worker shutting down");
    return 0;
}

void worker_stop(worker_t *worker)
{
    if (!worker) return;
    
    log_info("Stop requested for worker");
    
    pthread_mutex_lock(&worker->mutex);
    worker->running = 0;
    pthread_mutex_unlock(&worker->mutex);
}

int worker_can_accept_task(worker_t *worker)
{
    if (!worker) return 0;
    
    pthread_mutex_lock(&worker->mutex);
    
    int can_accept = 1;
    
    /* Check if already processing task */
    if (worker->task_active) {
        can_accept = 0;
    }
    
    /* Check resource limits */
    if (can_accept && worker->resources) {
        limit_status_t status = resource_limits_check_all(worker->resources);
        if (status == LIMIT_EXCEEDED || status == LIMIT_CRITICAL) {
            can_accept = 0;
        }
    }
    
    pthread_mutex_unlock(&worker->mutex);
    
    return can_accept;
}

void worker_get_stats(worker_t *worker, worker_stats_t *stats)
{
    if (!worker || !stats) return;
    
    pthread_mutex_lock(&worker->mutex);
    memcpy(stats, &worker->stats, sizeof(worker_stats_t));
    
    if (worker->resources) {
        resource_limits_get_stats(worker->resources, &stats->resource_stats);
    }
    
    pthread_mutex_unlock(&worker->mutex);
}