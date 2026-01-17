/*
 * Distributed Computing Project - Scheduler Implementation
 */

#include "scheduler.h"
#include "../common/log.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Maximum tracked tasks */
#define MAX_TRACKED_TASKS 100000

/* Scheduler structure */
struct scheduler {
    /* Configuration */
    scheduling_policy_t policy;
    int heartbeat_timeout_secs;
    size_t max_workers;
    
    /* Worker registry */
    worker_info_t *workers;
    size_t worker_count;
    
    /* Task tracking */
    task_info_t *tasks;
    size_t task_count;
    size_t max_tasks;
    
    /* Statistics */
    scheduler_stats_t stats;
    
    /* Thread safety */
    pthread_mutex_t mutex;
};

scheduler_t *scheduler_create(const scheduler_config_t *config)
{
    if (!config) {
        log_error("scheduler_create: NULL config");
        return NULL;
    }
    
    scheduler_t *sched = calloc(1, sizeof(*sched));
    if (!sched) {
        log_error("Failed to allocate scheduler");
        return NULL;
    }
    
    sched->policy = config->scheduling_policy;
    sched->heartbeat_timeout_secs = config->heartbeat_timeout_secs;
    sched->max_workers = config->max_workers;
    
    /* Allocate worker array */
    sched->workers = calloc(config->max_workers, sizeof(worker_info_t));
    if (!sched->workers) {
        log_error("Failed to allocate worker array");
        free(sched);
        return NULL;
    }
    
    /* Allocate task tracking array */
    sched->max_tasks = MAX_TRACKED_TASKS;
    sched->tasks = calloc(sched->max_tasks, sizeof(task_info_t));
    if (!sched->tasks) {
        log_error("Failed to allocate task tracking array");
        free(sched->workers);
        free(sched);
        return NULL;
    }
    
    pthread_mutex_init(&sched->mutex, NULL);
    
    log_info("Created scheduler (policy=%d, max_workers=%zu, timeout=%d secs)",
             sched->policy, sched->max_workers, sched->heartbeat_timeout_secs);
    
    return sched;
}

void scheduler_destroy(scheduler_t *sched)
{
    if (!sched) return;
    
    log_info("Destroying scheduler (workers=%zu, tasks_completed=%lu)",
             sched->worker_count, sched->stats.tasks_completed);
    
    pthread_mutex_destroy(&sched->mutex);
    free(sched->tasks);
    free(sched->workers);
    free(sched);
}

int scheduler_register_worker(scheduler_t *sched, const worker_info_t *info)
{
    if (!sched || !info) return -1;
    
    pthread_mutex_lock(&sched->mutex);
    
    if (sched->worker_count >= sched->max_workers) {
        pthread_mutex_unlock(&sched->mutex);
        log_error("Maximum worker count reached: %zu", sched->max_workers);
        return -2;
    }
    
    /* Check if worker already registered */
    for (size_t i = 0; i < sched->worker_count; i++) {
        if (sched->workers[i].worker_id == info->worker_id) {
            /* Update existing worker */
            sched->workers[i] = *info;
            sched->workers[i].state = WORKER_STATE_IDLE;
            sched->workers[i].last_heartbeat = time(NULL);
            pthread_mutex_unlock(&sched->mutex);
            log_info("Updated worker %u registration", info->worker_id);
            return 0;
        }
    }
    
    /* Add new worker */
    sched->workers[sched->worker_count] = *info;
    sched->workers[sched->worker_count].state = WORKER_STATE_IDLE;
    sched->workers[sched->worker_count].last_heartbeat = time(NULL);
    sched->workers[sched->worker_count].registered = time(NULL);
    sched->worker_count++;
    sched->stats.total_workers++;
    sched->stats.active_workers++;
    sched->stats.idle_workers++;
    
    pthread_mutex_unlock(&sched->mutex);
    
    log_info("Registered worker %u (total=%zu, local_ranks=%u)",
             info->worker_id, sched->worker_count, info->num_local_ranks);
    
    return 0;
}

int scheduler_unregister_worker(scheduler_t *sched, uint32_t worker_id)
{
    if (!sched) return -1;
    
    pthread_mutex_lock(&sched->mutex);
    
    for (size_t i = 0; i < sched->worker_count; i++) {
        if (sched->workers[i].worker_id == worker_id) {
            /* Update stats */
            if (sched->workers[i].state != WORKER_STATE_DEAD) {
                sched->stats.active_workers--;
                if (sched->workers[i].state == WORKER_STATE_IDLE) {
                    sched->stats.idle_workers--;
                } else if (sched->workers[i].state == WORKER_STATE_BUSY) {
                    sched->stats.busy_workers--;
                }
            }
            
            /* Remove worker (swap with last) */
            if (i < sched->worker_count - 1) {
                sched->workers[i] = sched->workers[sched->worker_count - 1];
            }
            sched->worker_count--;
            
            pthread_mutex_unlock(&sched->mutex);
            log_info("Unregistered worker %u", worker_id);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&sched->mutex);
    log_warn("Attempted to unregister unknown worker %u", worker_id);
    return -2;
}

void scheduler_update_heartbeat(scheduler_t *sched, uint32_t worker_id)
{
    if (!sched) return;
    
    pthread_mutex_lock(&sched->mutex);
    
    for (size_t i = 0; i < sched->worker_count; i++) {
        if (sched->workers[i].worker_id == worker_id) {
            sched->workers[i].last_heartbeat = time(NULL);
            
            /* Revive dead worker */
            if (sched->workers[i].state == WORKER_STATE_DEAD) {
                sched->workers[i].state = WORKER_STATE_IDLE;
                sched->stats.dead_workers--;
                sched->stats.active_workers++;
                sched->stats.idle_workers++;
                log_info("Worker %u revived", worker_id);
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&sched->mutex);
}

void scheduler_mark_worker_busy(scheduler_t *sched, uint32_t worker_id, 
                                size_t task_count)
{
    if (!sched) return;
    
    pthread_mutex_lock(&sched->mutex);
    
    for (size_t i = 0; i < sched->worker_count; i++) {
        if (sched->workers[i].worker_id == worker_id) {
            if (sched->workers[i].state == WORKER_STATE_IDLE) {
                sched->workers[i].state = WORKER_STATE_BUSY;
                sched->stats.idle_workers--;
                sched->stats.busy_workers++;
            }
            sched->workers[i].tasks_running += task_count;
            break;
        }
    }
    
    pthread_mutex_unlock(&sched->mutex);
}

void scheduler_mark_worker_idle(scheduler_t *sched, uint32_t worker_id)
{
    if (!sched) return;
    
    pthread_mutex_lock(&sched->mutex);
    
    for (size_t i = 0; i < sched->worker_count; i++) {
        if (sched->workers[i].worker_id == worker_id) {
            if (sched->workers[i].state == WORKER_STATE_BUSY) {
                sched->workers[i].state = WORKER_STATE_IDLE;
                sched->stats.busy_workers--;
                sched->stats.idle_workers++;
            }
            sched->workers[i].tasks_running = 0;
            break;
        }
    }
    
    pthread_mutex_unlock(&sched->mutex);
}

int scheduler_get_idle_workers(scheduler_t *sched, worker_info_t *workers,
                               size_t max_workers)
{
    if (!sched || !workers) return 0;
    
    pthread_mutex_lock(&sched->mutex);
    
    int count = 0;
    for (size_t i = 0; i < sched->worker_count && count < (int)max_workers; i++) {
        if (sched->workers[i].state == WORKER_STATE_IDLE) {
            workers[count++] = sched->workers[i];
        }
    }
    
    pthread_mutex_unlock(&sched->mutex);
    
    return count;
}

int scheduler_track_task(scheduler_t *sched, uint64_t task_id, 
                         uint32_t worker_id)
{
    if (!sched) return -1;
    
    pthread_mutex_lock(&sched->mutex);
    
    if (sched->task_count >= sched->max_tasks) {
        pthread_mutex_unlock(&sched->mutex);
        log_warn("Task tracking limit reached: %zu", sched->max_tasks);
        return -2;
    }
    
    task_info_t *task = &sched->tasks[sched->task_count++];
    task->task_id = task_id;
    task->worker_id = worker_id;
    task->submitted = time(NULL);
    task->started = time(NULL);
    task->status = -1; /* Pending */
    
    sched->stats.tasks_scheduled++;
    
    pthread_mutex_unlock(&sched->mutex);
    
    return 0;
}

int scheduler_mark_task_complete(scheduler_t *sched, uint64_t task_id, 
                                 int status)
{
    if (!sched) return -1;
    
    pthread_mutex_lock(&sched->mutex);
    
    /* Find task */
    for (size_t i = 0; i < sched->task_count; i++) {
        if (sched->tasks[i].task_id == task_id) {
            sched->tasks[i].status = status;
            
            /* Update worker stats */
            uint32_t worker_id = sched->tasks[i].worker_id;
            for (size_t j = 0; j < sched->worker_count; j++) {
                if (sched->workers[j].worker_id == worker_id) {
                    if (sched->workers[j].tasks_running > 0) {
                        sched->workers[j].tasks_running--;
                    }
                    
                    if (status == 0) {
                        sched->workers[j].tasks_completed++;
                        sched->stats.tasks_completed++;
                    } else {
                        sched->workers[j].tasks_failed++;
                        sched->stats.tasks_failed++;
                    }
                    
                    /* Mark idle if no more tasks */
                    if (sched->workers[j].tasks_running == 0 &&
                        sched->workers[j].state == WORKER_STATE_BUSY) {
                        sched->workers[j].state = WORKER_STATE_IDLE;
                        sched->stats.busy_workers--;
                        sched->stats.idle_workers++;
                    }
                    break;
                }
            }
            
            pthread_mutex_unlock(&sched->mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&sched->mutex);
    log_debug("Task %lu not found in tracking", task_id);
    return -2;
}

int scheduler_check_timeouts(scheduler_t *sched)
{
    if (!sched) return 0;
    
    pthread_mutex_lock(&sched->mutex);
    
    time_t now = time(NULL);
    int dead_count = 0;
    
    for (size_t i = 0; i < sched->worker_count; i++) {
        if (sched->workers[i].state == WORKER_STATE_DEAD) {
            continue; /* Already dead */
        }
        
        time_t last_contact = now - sched->workers[i].last_heartbeat;
        if (last_contact > sched->heartbeat_timeout_secs) {
            log_warn("Worker %u timed out (last contact %ld secs ago)",
                    sched->workers[i].worker_id, last_contact);
            
            /* Update stats */
            sched->stats.active_workers--;
            if (sched->workers[i].state == WORKER_STATE_IDLE) {
                sched->stats.idle_workers--;
            } else if (sched->workers[i].state == WORKER_STATE_BUSY) {
                sched->stats.busy_workers--;
            }
            sched->stats.dead_workers++;
            
            sched->workers[i].state = WORKER_STATE_DEAD;
            dead_count++;
        }
    }
    
    pthread_mutex_unlock(&sched->mutex);
    
    return dead_count;
}

void scheduler_get_stats(scheduler_t *sched, scheduler_stats_t *stats)
{
    if (!sched || !stats) return;
    
    pthread_mutex_lock(&sched->mutex);
    *stats = sched->stats;
    pthread_mutex_unlock(&sched->mutex);
}