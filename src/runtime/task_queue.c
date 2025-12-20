#include "task_queue.h"
#include "../common/log.h"
#include "../../include/project.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* Task queue node */
typedef struct task_node {
    task_t task;
    struct task_node *next;
    int priority;  /* 0 = normal, 1 = high */
} task_node_t;

/* Task queue structure */
struct task_queue {
    task_node_t *head;
    task_node_t *tail;
    size_t size;
    size_t max_size;
    int allow_duplicates;
    int shutting_down;
    
    /* Statistics */
    uint64_t total_enqueued;
    uint64_t total_dequeued;
    uint64_t high_priority_count;
    uint64_t blocked_threads;
    
    /* Thread synchronization */
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
};

/* Default configuration */
static const task_queue_config_t default_config = {
    .max_size = MAX_TASK_QUEUE_SIZE,
    .allow_duplicates = 1
};

task_queue_t *task_queue_create(const task_queue_config_t *config)
{
    const task_queue_config_t *cfg = config ? config : &default_config;
    
    task_queue_t *queue = calloc(1, sizeof(*queue));
    if (!queue) {
        log_error("Failed to allocate task queue");
        return NULL;
    }
    
    queue->max_size = cfg->max_size;
    queue->allow_duplicates = cfg->allow_duplicates;
    queue->shutting_down = 0;
    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond_not_empty, NULL);
    pthread_cond_init(&queue->cond_not_full, NULL);
    
    log_info("Created task queue (max_size=%zu, allow_duplicates=%d)",
             queue->max_size, queue->allow_duplicates);
    
    return queue;
}

void task_queue_destroy(task_queue_t *queue)
{
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    /* Mark as shutting down and wake all waiting threads */
    queue->shutting_down = 1;
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_cond_broadcast(&queue->cond_not_full);
    
    /* Free all remaining tasks */
    task_node_t *node = queue->head;
    size_t freed = 0;
    while (node) {
        task_node_t *next = node->next;
        free(node);
        node = next;
        freed++;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    
    log_info("Destroyed task queue (freed %zu tasks, total_enqueued=%lu, total_dequeued=%lu)",
             freed, queue->total_enqueued, queue->total_dequeued);
    
    free(queue);
}

/* Helper: check if task ID exists in queue (must hold mutex) */
static int task_exists(task_queue_t *queue, uint64_t task_id)
{
    task_node_t *node = queue->head;
    while (node) {
        if (node->task.task_id == task_id) {
            return 1;
        }
        node = node->next;
    }
    return 0;
}

int task_queue_enqueue(task_queue_t *queue, const task_t *task)
{
    if (!queue || !task) {
        log_error("task_queue_enqueue: NULL argument");
        return -1;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->shutting_down) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    /* Check if queue is full */
    if (queue->max_size > 0 && queue->size >= queue->max_size) {
        pthread_mutex_unlock(&queue->mutex);
        log_warn("Task queue full (size=%zu, max=%zu)", queue->size, queue->max_size);
        return -2;
    }
    
    /* Check for duplicates if not allowed */
    if (!queue->allow_duplicates && task_exists(queue, task->task_id)) {
        pthread_mutex_unlock(&queue->mutex);
        log_warn("Duplicate task ID %lu rejected", task->task_id);
        return -3;
    }
    
    /* Allocate new node */
    task_node_t *node = malloc(sizeof(*node));
    if (!node) {
        pthread_mutex_unlock(&queue->mutex);
        log_error("Failed to allocate task node");
        return -1;
    }
    
    memcpy(&node->task, task, sizeof(task_t));
    node->next = NULL;
    node->priority = (task->flags & TASK_FLAG_HIGH_PRIORITY) ? 1 : 0;
    
    /* Insert based on priority */
    if (node->priority == 1) {
        /* High priority: insert after last high-priority task */
        if (!queue->head || queue->head->priority == 0) {
            /* Queue empty or all normal priority - insert at head */
            node->next = queue->head;
            queue->head = node;
            if (!queue->tail) {
                queue->tail = node;
            }
        } else {
            /* Find last high-priority task */
            task_node_t *prev = NULL;
            task_node_t *curr = queue->head;
            while (curr && curr->priority == 1) {
                prev = curr;
                curr = curr->next;
            }
            /* Insert after prev */
            node->next = curr;
            if (prev) {
                prev->next = node;
            }
            if (!node->next) {
                queue->tail = node;
            }
        }
        queue->high_priority_count++;
    } else {
        /* Normal priority: append to tail */
        if (!queue->head) {
            queue->head = queue->tail = node;
        } else {
            queue->tail->next = node;
            queue->tail = node;
        }
    }
    
    queue->size++;
    queue->total_enqueued++;
    
    /* Wake one waiting thread */
    pthread_cond_signal(&queue->cond_not_empty);
    
    pthread_mutex_unlock(&queue->mutex);
    
    log_debug("Enqueued task %lu (priority=%d, queue_size=%zu)",
              task->task_id, node->priority, queue->size);
    
    return 0;
}

int task_queue_dequeue(task_queue_t *queue, task_t *task)
{
    if (!queue || !task) {
        log_error("task_queue_dequeue: NULL argument");
        return -1;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    if (!queue->head) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    /* Remove head node */
    task_node_t *node = queue->head;
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    
    memcpy(task, &node->task, sizeof(task_t));
    
    if (node->priority == 1) {
        queue->high_priority_count--;
    }
    
    queue->size--;
    queue->total_dequeued++;
    
    free(node);
    
    /* Wake one waiting producer if queue was full */
    pthread_cond_signal(&queue->cond_not_full);
    
    pthread_mutex_unlock(&queue->mutex);
    
    log_debug("Dequeued task %lu (queue_size=%zu)", task->task_id, queue->size);
    
    return 0;
}

int task_queue_dequeue_wait(task_queue_t *queue, task_t *task, int timeout_ms)
{
    if (!queue || !task) {
        log_error("task_queue_dequeue_wait: NULL argument");
        return -2;
    }
    
    pthread_mutex_lock(&queue->mutex);
    queue->blocked_threads++;
    
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
    
    /* Wait for task or shutdown */
    while (!queue->head && !queue->shutting_down) {
        int rc;
        if (timeout_ms < 0) {
            rc = pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
        } else {
            rc = pthread_cond_timedwait(&queue->cond_not_empty, &queue->mutex, &ts);
        }
        
        if (rc == ETIMEDOUT) {
            queue->blocked_threads--;
            pthread_mutex_unlock(&queue->mutex);
            return -1;  /* Timeout */
        }
    }
    
    queue->blocked_threads--;
    
    if (queue->shutting_down) {
        pthread_mutex_unlock(&queue->mutex);
        return -2;  /* Queue destroyed */
    }
    
    /* Dequeue task */
    task_node_t *node = queue->head;
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    
    memcpy(task, &node->task, sizeof(task_t));
    
    if (node->priority == 1) {
        queue->high_priority_count--;
    }
    
    queue->size--;
    queue->total_dequeued++;
    
    free(node);
    
    pthread_cond_signal(&queue->cond_not_full);
    
    pthread_mutex_unlock(&queue->mutex);
    
    log_debug("Dequeued task %lu after wait (queue_size=%zu)", task->task_id, queue->size);
    
    return 0;
}

int task_queue_peek(task_queue_t *queue, task_t *task)
{
    if (!queue || !task) {
        return -1;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    if (!queue->head) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    memcpy(task, &queue->head->task, sizeof(task_t));
    
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

int task_queue_remove_by_id(task_queue_t *queue, uint64_t task_id)
{
    if (!queue) {
        return -1;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    task_node_t *prev = NULL;
    task_node_t *curr = queue->head;
    
    while (curr) {
        if (curr->task.task_id == task_id) {
            /* Found it - remove */
            if (prev) {
                prev->next = curr->next;
            } else {
                queue->head = curr->next;
            }
            
            if (curr == queue->tail) {
                queue->tail = prev;
            }
            
            if (curr->priority == 1) {
                queue->high_priority_count--;
            }
            
            queue->size--;
            free(curr);
            
            pthread_cond_signal(&queue->cond_not_full);
            
            pthread_mutex_unlock(&queue->mutex);
            
            log_debug("Removed task %lu from queue", task_id);
            return 0;
        }
        
        prev = curr;
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    return -1;  /* Not found */
}

int task_queue_is_empty(const task_queue_t *queue)
{
    if (!queue) return 1;
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->mutex);
    int empty = (queue->size == 0);
    pthread_mutex_unlock((pthread_mutex_t*)&queue->mutex);
    
    return empty;
}

int task_queue_is_full(const task_queue_t *queue)
{
    if (!queue) return 0;
    if (queue->max_size == 0) return 0;  /* Unlimited */
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->mutex);
    int full = (queue->size >= queue->max_size);
    pthread_mutex_unlock((pthread_mutex_t*)&queue->mutex);
    
    return full;
}

size_t task_queue_size(const task_queue_t *queue)
{
    if (!queue) return 0;
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->mutex);
    size_t size = queue->size;
    pthread_mutex_unlock((pthread_mutex_t*)&queue->mutex);
    
    return size;
}

void task_queue_get_stats(const task_queue_t *queue, task_queue_stats_t *stats)
{
    if (!queue || !stats) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->mutex);
    
    stats->current_size = queue->size;
    stats->max_size = queue->max_size;
    stats->total_enqueued = queue->total_enqueued;
    stats->total_dequeued = queue->total_dequeued;
    stats->high_priority_count = queue->high_priority_count;
    stats->blocked_threads = queue->blocked_threads;
    
    pthread_mutex_unlock((pthread_mutex_t*)&queue->mutex);
}

size_t task_queue_clear(task_queue_t *queue)
{
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    
    size_t count = 0;
    task_node_t *node = queue->head;
    while (node) {
        task_node_t *next = node->next;
        free(node);
        node = next;
        count++;
    }
    
    queue->head = queue->tail = NULL;
    queue->size = 0;
    queue->high_priority_count = 0;
    
    pthread_cond_broadcast(&queue->cond_not_full);
    
    pthread_mutex_unlock(&queue->mutex);
    
    log_info("Cleared %zu tasks from queue", count);
    
    return count;
}

void task_queue_wake_all(task_queue_t *queue)
{
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
}