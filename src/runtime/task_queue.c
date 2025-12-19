#include "task_queue.h"
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include "common/log.h"

struct task_queue {
    task_t **buffer;        // array of task pointers
    size_t capacity;        // total capacity
    size_t head;            // index of the first element
    size_t tail;            // index one past the last element
    size_t count;           // current number of elements
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

static int expand_buffer(task_queue_t *q)
{
    size_t new_cap = q->capacity * 2;
    if (new_cap < 4) new_cap = 4;
    task_t **nb = realloc(q->buffer, new_cap * sizeof(task_t *));
    if (!nb) return -1;

    // If wrapped, move elements so that head becomes 0
    if (q->head > q->tail) {
        size_t right_count = q->capacity - q->head;
        memmove(&nb[q->capacity], &nb[q->head], right_count * sizeof(task_t *));
        q->head = q->capacity;
    }

    q->buffer = nb;
    q->capacity = new_cap;
    log_debug("task_queue: expanded to capacity %zu", q->capacity);
    return 0;
}

task_queue_t *task_queue_create(size_t initial_capacity)
{
    if (initial_capacity == 0) initial_capacity = 16;

    task_queue_t *q = calloc(1, sizeof(*q));
    if (!q) return NULL;

    q->buffer = calloc(initial_capacity, sizeof(task_t *));
    if (!q->buffer) { free(q); return NULL; }

    q->capacity = initial_capacity;
    q->head = q->tail = q->count = 0;

    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        free(q->buffer);
        free(q);
        return NULL;
    }
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);

    return q;
}

void task_queue_destroy(task_queue_t *q)
{
    if (!q) return;
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->buffer);
    free(q);
}

int task_queue_push(task_queue_t *q, task_t *task)
{
    if (!q || !task) return -1;

    pthread_mutex_lock(&q->lock);
    while (q->count == q->capacity) {
        // try to expand; if expand fails, wait for space
        if (expand_buffer(q) == 0) break;
        // expansion failed; wait until not_full
        pthread_cond_wait(&q->not_full, &q->lock);
    }

    q->buffer[q->tail] = task;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

task_t *task_queue_try_pop(task_queue_t *q)
{
    if (!q) return NULL;
    task_t *t = NULL;

    pthread_mutex_lock(&q->lock);
    if (q->count > 0) {
        t = q->buffer[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        pthread_cond_signal(&q->not_full);
    }
    pthread_mutex_unlock(&q->lock);
    return t;
}

task_t *task_queue_pop(task_queue_t *q)
{
    if (!q) return NULL;
    task_t *t = NULL;

    pthread_mutex_lock(&q->lock);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    t = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return t;
}

task_t *task_queue_peek(task_queue_t *q)
{
    if (!q) return NULL;
    task_t *t = NULL;
    pthread_mutex_lock(&q->lock);
    if (q->count > 0) t = q->buffer[q->head];
    pthread_mutex_unlock(&q->lock);
    return t;
}

size_t task_queue_size(const task_queue_t *q)
{
    if (!q) return 0;
    // const cast to access lock safely
    task_queue_t *qq = (task_queue_t *)q;
    pthread_mutex_lock(&qq->lock);
    size_t c = qq->count;
    pthread_mutex_unlock(&qq->lock);
    return c;
}

bool task_queue_is_empty(const task_queue_t *q)
{
    return task_queue_size(q) == 0;
}