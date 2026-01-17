#include "coordinator.h"
#include "../common/log.h"
#include "../storage/metadata.h"
#include "../../include/project.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

/* Worker registry entry */
typedef struct worker_info {
    uint32_t worker_id;
    int rank;                       /* MPI rank or ZMQ ID */
    uint64_t last_heartbeat_ms;
    uint64_t tasks_completed;
    uint64_t tasks_failed;
    task_state_t current_task_state;
    uint64_t current_task_id;
    int active;
    char identity[64];              /* For ZMQ workers */
} worker_info_t;

/* Task tracking */
typedef struct task_tracking {
    uint64_t task_id;
    task_state_t state;
    uint32_t assigned_worker;
    uint64_t submit_time_ms;
    uint64_t dispatch_time_ms;
    task_result_t result;
    int result_ready;
} task_tracking_t;

/* Batch for network workers */
typedef struct task_batch {
    task_t tasks[64];              /* Up to 64 tasks per batch */
    size_t count;
    uint64_t created_ms;
} task_batch_t;

/* Coordinator structure */
struct coordinator {
    /* Configuration */
    coordinator_config_t config;
    
    /* Transport layer */
    transport_t *transport;
    
    /* Task management */
    task_queue_t *pending_queue;
    task_tracking_t *tracking;     /* Hash table would be better */
    size_t tracking_capacity;
    size_t tracking_count;
    
    /* Worker registry */
    worker_info_t workers[MAX_WORKERS];
    uint32_t worker_count;
    
    /* Batching for network workers */
    task_batch_t *current_batch;
    
    /* Result storage */
    metadata_store_t *metadata;
    
    /* Statistics */
    coordinator_stats_t stats;
    
    /* Control */
    int running;
    int shutdown_requested;
    
    /* Thread synchronization */
    pthread_mutex_t mutex;
    pthread_cond_t task_complete_cond;
    pthread_t dispatch_thread;
    pthread_t heartbeat_thread;
};

/* Helper: get current time in milliseconds */
static uint64_t current_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Helper: find worker by rank/ID */
static worker_info_t *find_worker(coordinator_t *coord, int rank)
{
    for (uint32_t i = 0; i < coord->worker_count; ++i) {
        if (coord->workers[i].rank == rank && coord->workers[i].active) {
            return &coord->workers[i];
        }
    }
    return NULL;
}

/* Helper: register new worker */
static worker_info_t *register_worker(coordinator_t *coord, int rank, const char *identity)
{
    if (coord->worker_count >= coord->config.max_workers) {
        log_error("Maximum worker count reached (%u)", coord->config.max_workers);
        return NULL;
    }
    
    worker_info_t *worker = &coord->workers[coord->worker_count];
    memset(worker, 0, sizeof(*worker));
    
    worker->worker_id = coord->worker_count;
    worker->rank = rank;
    worker->last_heartbeat_ms = current_time_ms();
    worker->active = 1;
    
    if (identity) {
        strncpy(worker->identity, identity, sizeof(worker->identity) - 1);
    }
    
    coord->worker_count++;
    coord->stats.workers_total++;
    coord->stats.workers_active++;
    
    log_info("Registered worker %u (rank=%d, identity=%s)",
             worker->worker_id, rank, identity ? identity : "N/A");
    
    return worker;
}

/* Helper: find task tracking by ID */
static task_tracking_t *find_task_tracking(coordinator_t *coord, uint64_t task_id)
{
    for (size_t i = 0; i < coord->tracking_count; ++i) {
        if (coord->tracking[i].task_id == task_id) {
            return &coord->tracking[i];
        }
    }
    return NULL;
}

/* Helper: add task tracking */
static task_tracking_t *add_task_tracking(coordinator_t *coord, uint64_t task_id)
{
    if (coord->tracking_count >= coord->tracking_capacity) {
        /* Expand tracking array */
        size_t new_capacity = coord->tracking_capacity * 2;
        task_tracking_t *new_tracking = realloc(coord->tracking,
                                                new_capacity * sizeof(task_tracking_t));
        if (!new_tracking) {
            log_error("Failed to expand task tracking array");
            return NULL;
        }
        coord->tracking = new_tracking;
        coord->tracking_capacity = new_capacity;
    }
    
    task_tracking_t *track = &coord->tracking[coord->tracking_count++];
    memset(track, 0, sizeof(*track));
    track->task_id = task_id;
    track->state = TASK_STATE_QUEUED;
    track->submit_time_ms = current_time_ms();
    
    return track;
}

/* Dispatch thread - assigns tasks to workers */
static void *dispatch_thread_func(void *arg)
{
    coordinator_t *coord = (coordinator_t *)arg;
    
    log_info("Dispatch thread started");
    
    while (coord->running) {
        pthread_mutex_lock(&coord->mutex);
        
        /* Check for available workers */
        worker_info_t *available_worker = NULL;
        for (uint32_t i = 0; i < coord->worker_count; ++i) {
            if (coord->workers[i].active &&
                coord->workers[i].current_task_state == TASK_STATE_COMPLETED) {
                available_worker = &coord->workers[i];
                break;
            }
        }
        
        if (!available_worker) {
            pthread_mutex_unlock(&coord->mutex);
            usleep(10000);  /* 10ms */
            continue;
        }
        
        /* Get next task from queue */
        task_t task;
        int rc = task_queue_dequeue(coord->pending_queue, &task);
        if (rc != 0) {
            pthread_mutex_unlock(&coord->mutex);
            usleep(10000);
            continue;
        }
        
        /* Create tracking entry */
        task_tracking_t *track = find_task_tracking(coord, task.task_id);
        if (!track) {
            track = add_task_tracking(coord, task.task_id);
        }
        
        if (track) {
            track->state = TASK_STATE_DISPATCHED;
            track->assigned_worker = available_worker->worker_id;
            track->dispatch_time_ms = current_time_ms();
        }
        
        /* Send task to worker */
        message_t *msg = message_alloc(sizeof(task_t));
        if (msg) {
            message_set_header(msg, MSG_TYPE_TASK_SUBMIT, task.task_id);
            memcpy(msg->payload, &task, sizeof(task_t));
            msg->header.payload_len = sizeof(task_t);
            
            rc = transport_send(coord->transport, msg, available_worker->rank);
            message_free(msg);
            
            if (rc == 0) {
                available_worker->current_task_id = task.task_id;
                available_worker->current_task_state = TASK_STATE_RUNNING;
                
                log_debug("Dispatched task %lu to worker %u",
                         task.task_id, available_worker->worker_id);
            } else {
                log_error("Failed to send task %lu to worker %u",
                         task.task_id, available_worker->worker_id);
                /* Re-queue task */
                task_queue_enqueue(coord->pending_queue, &task);
            }
        }
        
        pthread_mutex_unlock(&coord->mutex);
    }
    
    log_info("Dispatch thread stopped");
    return NULL;
}

/* Heartbeat thread - monitors worker health */
static void *heartbeat_thread_func(void *arg)
{
    coordinator_t *coord = (coordinator_t *)arg;
    
    log_info("Heartbeat monitor thread started");
    
    while (coord->running) {
        sleep(coord->config.worker_timeout_ms / 1000);
        
        pthread_mutex_lock(&coord->mutex);
        
        uint64_t now = current_time_ms();
        uint32_t timeout_threshold = coord->config.worker_timeout_ms;
        
        for (uint32_t i = 0; i < coord->worker_count; ++i) {
            worker_info_t *worker = &coord->workers[i];
            
            if (!worker->active) continue;
            
            uint64_t elapsed = now - worker->last_heartbeat_ms;
            
            if (elapsed > timeout_threshold) {
                log_warn("Worker %u timed out (last heartbeat %lu ms ago)",
                        worker->worker_id, elapsed);
                
                worker->active = 0;
                coord->stats.workers_active--;
                
                /* Re-queue task if worker was executing one */
                if (worker->current_task_state == TASK_STATE_RUNNING) {
                    task_tracking_t *track = find_task_tracking(coord,
                                                               worker->current_task_id);
                    if (track) {
                        log_info("Re-queuing task %lu from timed-out worker %u",
                                worker->current_task_id, worker->worker_id);
                        track->state = TASK_STATE_QUEUED;
                        /* Would need to re-construct task_t from tracking */
                    }
                }
            }
        }
        
        pthread_mutex_unlock(&coord->mutex);
    }
    
    log_info("Heartbeat monitor thread stopped");
    return NULL;
}

/* Handle incoming messages */
static int handle_message(coordinator_t *coord, message_t *msg, int src_rank)
{
    pthread_mutex_lock(&coord->mutex);
    
    switch (msg->header.msg_type) {
        case MSG_TYPE_WORKER_REGISTER: {
            worker_info_t *worker = find_worker(coord, src_rank);
            if (!worker) {
                worker = register_worker(coord, src_rank, NULL);
            }
            if (worker) {
                worker->last_heartbeat_ms = current_time_ms();
            }
            break;
        }
        
        case MSG_TYPE_HEARTBEAT: {
            worker_info_t *worker = find_worker(coord, src_rank);
            if (worker) {
                worker->last_heartbeat_ms = current_time_ms();
            }
            break;
        }
        
        case MSG_TYPE_TASK_RESULT: {
            if (msg->header.payload_len < sizeof(task_result_t)) {
                log_error("Invalid task result message size");
                break;
            }
            
            task_result_t *result = (task_result_t *)msg->payload;
            uint64_t task_id = result->task_id;
            
            task_tracking_t *track = find_task_tracking(coord, task_id);
            if (track) {
                memcpy(&track->result, result, sizeof(task_result_t));
                track->state = (result->status == OK) ?
                              TASK_STATE_COMPLETED : TASK_STATE_FAILED;
                track->result_ready = 1;
                
                if (result->status == OK) {
                    coord->stats.tasks_completed++;
                } else {
                    coord->stats.tasks_failed++;
                }
                
                /* Update worker stats */
                worker_info_t *worker = find_worker(coord, src_rank);
                if (worker) {
                    worker->current_task_state = TASK_STATE_COMPLETED;
                    if (result->status == OK) {
                        worker->tasks_completed++;
                    } else {
                        worker->tasks_failed++;
                    }
                }
                
                /* Store metadata */
                if (coord->metadata) {
                    metadata_store_task_info(coord->metadata,
                                            task_id,
                                            0,  /* model_id from task */
                                            result->status,
                                            result->exec_time_us,
                                            result->queue_time_us,
                                            result->worker_id);
                }
                
                log_info("Received result for task %lu: status=%s exec_time=%lu us",
                        task_id,
                        status_to_string(result->status),
                        result->exec_time_us);
                
                /* Signal waiting threads */
                pthread_cond_broadcast(&coord->task_complete_cond);
            }
            break;
        }
        
        case MSG_TYPE_WORKER_SHUTDOWN: {
            worker_info_t *worker = find_worker(coord, src_rank);
            if (worker) {
                log_info("Worker %u shutting down gracefully", worker->worker_id);
                worker->active = 0;
                coord->stats.workers_active--;
            }
            break;
        }
        
        default:
            log_warn("Unhandled message type: %s",
                    message_type_to_string(msg->header.msg_type));
    }
    
    pthread_mutex_unlock(&coord->mutex);
    return 0;
}

coordinator_t *coordinator_create(const coordinator_config_t *config)
{
    if (!config) {
        log_error("coordinator_create: NULL config");
        return NULL;
    }
    
    coordinator_t *coord = calloc(1, sizeof(*coord));
    if (!coord) {
        log_error("Failed to allocate coordinator");
        return NULL;
    }
    
    memcpy(&coord->config, config, sizeof(coordinator_config_t));
    
    /* Create task queue */
    task_queue_config_t queue_cfg = {
        .max_size = config->task_queue_size,
        .allow_duplicates = 0
    };
    coord->pending_queue = task_queue_create(&queue_cfg);
    if (!coord->pending_queue) {
        log_error("Failed to create task queue");
        free(coord);
        return NULL;
    }
    
    /* Initialise task tracking */
    coord->tracking_capacity = 1024;
    coord->tracking = calloc(coord->tracking_capacity, sizeof(task_tracking_t));
    if (!coord->tracking) {
        log_error("Failed to allocate task tracking");
        task_queue_destroy(coord->pending_queue);
        free(coord);
        return NULL;
    }
    
    /* Open metadata store */
    if (config->metadata_dir) {
        coord->metadata = metadata_store_open(config->metadata_dir, 1);
        if (!coord->metadata) {
            log_warn("Failed to open metadata store (continuing without it)");
        }
    }
    
    /* Initialise transport */
    transport_config_t transport_cfg = {
        .type = config->transport_type,
        .role = TRANSPORT_ROLE_COORDINATOR,
        .endpoint = config->endpoint,
        .timeout_ms = 1000,
        .max_workers = config->max_workers
    };
    
    if (transport_init(&coord->transport, &transport_cfg) != 0) {
        log_error("Failed to initialise transport");
        if (coord->metadata) metadata_store_close(coord->metadata);
        free(coord->tracking);
        task_queue_destroy(coord->pending_queue);
        free(coord);
        return NULL;
    }
    
    pthread_mutex_init(&coord->mutex, NULL);
    pthread_cond_init(&coord->task_complete_cond, NULL);
    
    log_info("Created coordinator (transport=%s, max_workers=%u, batch_size=%zu)",
             transport_type_to_string(config->transport_type),
             config->max_workers,
             config->batch_size);
    
    return coord;
}

void coordinator_destroy(coordinator_t *coord)
{
    if (!coord) return;
    
    log_info("Destroying coordinator");
    
    /* Stop threads */
    coord->running = 0;
    
    if (coord->dispatch_thread) {
        pthread_join(coord->dispatch_thread, NULL);
    }
    if (coord->heartbeat_thread) {
        pthread_join(coord->heartbeat_thread, NULL);
    }
    
    /* Send shutdown to all workers */
    message_t *shutdown_msg = message_alloc(0);
    if (shutdown_msg) {
        message_set_header(shutdown_msg, MSG_TYPE_WORKER_SHUTDOWN, 0);
        transport_broadcast(coord->transport, shutdown_msg);
        message_free(shutdown_msg);
    }
    
    /* Clean up */
    if (coord->transport) transport_shutdown(coord->transport);
    if (coord->pending_queue) task_queue_destroy(coord->pending_queue);
    if (coord->metadata) metadata_store_close(coord->metadata);
    
    free(coord->tracking);
    free(coord->current_batch);
    
    pthread_mutex_destroy(&coord->mutex);
    pthread_cond_destroy(&coord->task_complete_cond);
    
    free(coord);
    
    log_info("Coordinator destroyed");
}

int coordinator_submit_task(coordinator_t *coord, const task_t *task)
{
    if (!coord || !task) return -1;
    
    pthread_mutex_lock(&coord->mutex);
    
    if (coord->shutdown_requested) {
        pthread_mutex_unlock(&coord->mutex);
        log_warn("Cannot submit task - shutdown requested");
        return -3;
    }
    
    /* Add to pending queue */
    int rc = task_queue_enqueue(coord->pending_queue, task);
    if (rc != 0) {
        pthread_mutex_unlock(&coord->mutex);
        return rc;
    }
    
    /* Create tracking entry */
    add_task_tracking(coord, task->task_id);
    
    coord->stats.tasks_submitted++;
    
    pthread_mutex_unlock(&coord->mutex);
    
    log_debug("Submitted task %lu (queue_size=%zu)",
             task->task_id, task_queue_size(coord->pending_queue));
    
    return 0;
}

int coordinator_run(coordinator_t *coord)
{
    if (!coord) return -1;
    
    coord->running = 1;
    
    /* Start worker threads */
    pthread_create(&coord->dispatch_thread, NULL, dispatch_thread_func, coord);
    pthread_create(&coord->heartbeat_thread, NULL, heartbeat_thread_func, coord);
    
    log_info("Coordinator running (waiting for workers and tasks)");
    
    /* Main message loop */
    while (coord->running && !coord->shutdown_requested) {
        int poll_rc = transport_poll(coord->transport, 100);
        
        if (poll_rc > 0) {
            int src_rank = -1;
            message_t *msg = transport_recv(coord->transport, &src_rank);
            
            if (msg) {
                handle_message(coord, msg, src_rank);
                message_free(msg);
            }
        }
    }
    
    log_info("Coordinator shutting down");
    
    /* Wait for pending tasks to complete */
    log_info("Waiting for %zu pending tasks...", task_queue_size(coord->pending_queue));
    coordinator_wait_all(coord, 30000);  /* 30 second timeout */
    
    return 0;
}

void coordinator_stop(coordinator_t *coord)
{
    if (!coord) return;
    
    log_info("Stop requested for coordinator");
    
    pthread_mutex_lock(&coord->mutex);
    coord->shutdown_requested = 1;
    pthread_mutex_unlock(&coord->mutex);
}

int coordinator_wait_task(coordinator_t *coord,
                          uint64_t task_id,
                          task_result_t *result,
                          int timeout_ms)
{
    if (!coord || !result) return -2;
    
    struct timespec ts;
    if (timeout_ms >= 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
    }
    
    pthread_mutex_lock(&coord->mutex);
    
    while (1) {
        task_tracking_t *track = find_task_tracking(coord, task_id);
        
        if (!track) {
            pthread_mutex_unlock(&coord->mutex);
            return -2;  /* Task not found */
        }
        
        if (track->result_ready) {
            memcpy(result, &track->result, sizeof(task_result_t));
            pthread_mutex_unlock(&coord->mutex);
            return (track->result.status == OK) ? 0 : -2;
        }
        
        /* Wait for condition signal */
        int rc;
        if (timeout_ms < 0) {
            rc = pthread_cond_wait(&coord->task_complete_cond, &coord->mutex);
        } else {
            rc = pthread_cond_timedwait(&coord->task_complete_cond,
                                       &coord->mutex, &ts);
        }
        
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&coord->mutex);
            return -1;  /* Timeout */
        }
    }
}

int coordinator_wait_all(coordinator_t *coord, int timeout_ms)
{
    if (!coord) return -1;
    
    uint64_t start_time = current_time_ms();
    
    while (1) {
        pthread_mutex_lock(&coord->mutex);
        
        size_t pending = task_queue_size(coord->pending_queue);
        
        /* Count running tasks */
        size_t running = 0;
        for (size_t i = 0; i < coord->tracking_count; ++i) {
            if (coord->tracking[i].state == TASK_STATE_RUNNING ||
                coord->tracking[i].state == TASK_STATE_DISPATCHED) {
                running++;
            }
        }
        
        pthread_mutex_unlock(&coord->mutex);
        
        if (pending == 0 && running == 0) {
            log_info("All tasks completed");
            return 0;
        }
        
        if (timeout_ms >= 0) {
            uint64_t elapsed = current_time_ms() - start_time;
            if (elapsed >= (uint64_t)timeout_ms) {
                log_warn("Timeout waiting for tasks (pending=%zu, running=%zu)",
                        pending, running);
                return -1;
            }
        }
        
        usleep(100000);  /* 100ms */
    }
}

void coordinator_get_stats(coordinator_t *coord, coordinator_stats_t *stats)
{
    if (!coord || !stats) return;
    
    pthread_mutex_lock(&coord->mutex);
    memcpy(stats, &coord->stats, sizeof(coordinator_stats_t));
    pthread_mutex_unlock(&coord->mutex);
}

uint32_t coordinator_worker_count(coordinator_t *coord)
{
    if (!coord) return 0;
    
    pthread_mutex_lock(&coord->mutex);
    uint32_t count = coord->stats.workers_active;
    pthread_mutex_unlock(&coord->mutex);
    
    return count;
}