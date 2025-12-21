/*
 * Runtime Module Example
 * 
 * Demonstrates usage of task_queue and resource_limits
 * for the Distributed Computing Project.
 *
 * Compile:
 *   gcc -o runtime_example runtime_example.c \
 *       src/runtime/task_queue.c \
 *       src/runtime/resource_limits.c \
 *       src/common/log.c \
 *       src/common/errors.c \
 *       -I./include -I./src \
 *       -lpthread -lm
 */

#include "../../src/runtime/task_queue.h"
#include "../../src/runtime/resource_limits.h"
#include "../../src/common/log.h"
#include "../../src/common/errors.h"
#include "../../include/project.h"
#include "../../include/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

/* Example 1: Basic task queue operations */
static void example_task_queue_basic(void)
{
    printf("\n=== Example 1: Basic Task Queue ===\n");

    task_queue_config_t config = {
        .max_size = 10,
        .allow_duplicates = 0
    };

    task_queue_t *queue = task_queue_create(&config);
    if (!queue) {
        log_error("Failed to create task queue");
        return;
    }

    /* Enqueue some tasks */
    for (int i = 0; i < 5; i++) {
        task_t task = {
            .task_id = i + 1,
            .model_id = 100,
            .api_version = API_VERSION_CURRENT,
            .flags = (i % 2 == 0) ? TASK_FLAG_HIGH_PRIORITY : 0,
            .timeout_secs = 60
        };

        if (task_queue_enqueue(queue, &task) == 0) {
            printf("Enqueued task %lu (priority=%s)\n",
                   task.task_id,
                   (task.flags & TASK_FLAG_HIGH_PRIORITY) ? "HIGH" : "NORMAL");
        }
    }

    /* Get stats */
    task_queue_stats_t stats;
    task_queue_get_stats(queue, &stats);
    printf("\nQueue stats:\n");
    printf("  Current size: %zu\n", stats.current_size);
    printf("  High priority: %lu\n", stats.high_priority_count);
    printf("  Total enqueued: %lu\n", stats.total_enqueued);

    /* Dequeue all tasks */
    printf("\nDequeuing tasks (high priority first):\n");
    task_t task;
    while (task_queue_dequeue(queue, &task) == 0) {
        printf("  Task %lu (priority=%s)\n",
               task.task_id,
               (task.flags & TASK_FLAG_HIGH_PRIORITY) ? "HIGH" : "NORMAL");
    }

    task_queue_destroy(queue);
}

/* Example 2: Producer-consumer with task queue */
typedef struct {
    task_queue_t *queue;
    int producer_id;
    int num_tasks;
} producer_args_t;

typedef struct {
    task_queue_t *queue;
    int worker_id;
    int num_to_process;
} worker_args_t;

static void *producer_thread(void *arg)
{
    producer_args_t *args = (producer_args_t *)arg;

    log_info("Producer %d starting (tasks=%d)", args->producer_id, args->num_tasks);

    for (int i = 0; i < args->num_tasks; i++) {
        task_t task = {
            .task_id = args->producer_id * 1000 + i,
            .model_id = 100 + args->producer_id,
            .api_version = API_VERSION_CURRENT,
            .flags = (i % 5 == 0) ? TASK_FLAG_HIGH_PRIORITY : 0,
            .timeout_secs = 30
        };

        while (task_queue_enqueue(args->queue, &task) != 0) {
            /* Queue full - wait */
            usleep(10000);
        }

        log_debug("Producer %d enqueued task %lu", args->producer_id, task.task_id);

        /* Simulate variable production rate */
        usleep(rand() % 50000);
    }

    log_info("Producer %d finished", args->producer_id);
    return NULL;
}

static void *worker_thread(void *arg)
{
    worker_args_t *args = (worker_args_t *)arg;

    log_info("Worker %d starting", args->worker_id);

    int processed = 0;
    while (processed < args->num_to_process) {
        task_t task;
        int rc = task_queue_dequeue_wait(args->queue, &task, 5000);

        if (rc == 0) {
            /* Process task */
            log_debug("Worker %d processing task %lu", args->worker_id, task.task_id);

            /* Simulate work */
            usleep(rand() % 100000);

            processed++;
        } else if (rc == -1) {
            /* Timeout - check if we should exit */
            if (task_queue_is_empty(args->queue)) {
                break;
            }
        }
    }

    log_info("Worker %d processed %d tasks", args->worker_id, processed);
    return NULL;
}

static void example_producer_consumer(void)
{
    printf("\n=== Example 2: Producer-Consumer Pattern ===\n");

    task_queue_t *queue = task_queue_create(NULL);
    if (!queue) {
        log_error("Failed to create task queue");
        return;
    }

    const int num_producers = 2;
    const int num_workers = 3;
    const int tasks_per_producer = 10;

    pthread_t producers[num_producers];
    pthread_t workers[num_workers];

    producer_args_t prod_args[num_producers];
    worker_args_t work_args[num_workers];

    /* Start producers */
    for (int i = 0; i < num_producers; i++) {
        prod_args[i].queue = queue;
        prod_args[i].producer_id = i;
        prod_args[i].num_tasks = tasks_per_producer;
        pthread_create(&producers[i], NULL, producer_thread, &prod_args[i]);
    }

    /* Start workers */
    int tasks_per_worker = (num_producers * tasks_per_producer) / num_workers;
    for (int i = 0; i < num_workers; i++) {
        work_args[i].queue = queue;
        work_args[i].worker_id = i;
        work_args[i].num_to_process = tasks_per_worker;
        pthread_create(&workers[i], NULL, worker_thread, &work_args[i]);
    }

    /* Wait for producers */
    for (int i = 0; i < num_producers; i++) {
        pthread_join(producers[i], NULL);
    }

    printf("All producers finished\n");

    /* Wait for workers */
    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);
    }

    printf("All workers finished\n");

    /* Final stats */
    task_queue_stats_t stats;
    task_queue_get_stats(queue, &stats);
    printf("\nFinal queue stats:\n");
    printf("  Total enqueued: %lu\n", stats.total_enqueued);
    printf("  Total dequeued: %lu\n", stats.total_dequeued);
    printf("  Remaining: %zu\n", stats.current_size);

    task_queue_destroy(queue);
}

/* Example 3: Resource limiting */
static void example_resource_limits(void)
{
    printf("\n=== Example 3: Resource Limits ===\n");

    resource_limits_config_t config = {
        .max_memory_bytes = 512 * 1024 * 1024,  /* 512 MB */
        .warn_memory_bytes = 384 * 1024 * 1024,  /* 384 MB */
        .max_cpu_percent = 80.0,
        .max_concurrent_tasks = 4,
        .min_disk_space_bytes = 1ULL * 1024 * 1024 * 1024,  /* 1 GB */
        .enforce_hard_limits = 1
    };

    resource_limits_t *limits = resource_limits_create(&config);
    if (!limits) {
        log_error("Failed to create resource limits");
        return;
    }

    /* Update and display current usage */
    resource_limits_update(limits);

    resource_stats_t stats;
    resource_limits_get_stats(limits, &stats);

    printf("Current resource usage:\n");
    printf("  Memory (RSS): %.1f MB\n", stats.memory_rss_bytes / (1024.0 * 1024.0));
    printf("  Memory (Virtual): %.1f MB\n", stats.memory_virt_bytes / (1024.0 * 1024.0));
    printf("  Memory (Peak): %.1f MB\n", stats.memory_peak_bytes / (1024.0 * 1024.0));
    printf("  CPU usage: %.1f%%\n", stats.cpu_usage_percent);
    printf("  CPU time: %.2f seconds\n", stats.cpu_time_us / 1000000.0);
    printf("  Threads: %d\n", stats.num_threads);
    printf("  Open files: %d\n", stats.open_files);

    /* Check limits */
    printf("\nResource limit checks:\n");
    const char *resources[] = {"Memory", "CPU", "Disk", "Files", "Tasks"};
    resource_type_t types[] = {
        RESOURCE_MEMORY,
        RESOURCE_CPU_TIME,
        RESOURCE_DISK_SPACE,
        RESOURCE_OPEN_FILES,
        RESOURCE_TASKS_RUNNING
    };

    for (int i = 0; i < 5; i++) {
        limit_status_t status = resource_limits_check(limits, types[i]);
        printf("  %s: %s\n", resources[i], resource_limit_status_string(status));
    }

    /* Simulate reserving tasks */
    printf("\nSimulating task execution:\n");
    for (int i = 0; i < 5; i++) {
        int rc = resource_limits_reserve_task(limits);
        if (rc == 0) {
            printf("  Reserved resources for task %d\n", i + 1);

            /* Simulate work */
            usleep(100000);

            /* Complete task */
            resource_limits_release_task(limits, 1);
            printf("  Released resources for task %d\n", i + 1);
        } else {
            printf("  Failed to reserve resources for task %d\n", i + 1);
            break;
        }
    }

    /* Final stats */
    resource_limits_get_stats(limits, &stats);
    printf("\nFinal statistics:\n");
    printf("  Tasks completed: %lu\n", stats.tasks_completed);
    printf("  Tasks failed: %lu\n", stats.tasks_failed);

    resource_limits_destroy(limits);
}

/* Example 4: Integrated task queue + resource limits */
static void example_integrated(void)
{
    printf("\n=== Example 4: Integrated Task Queue + Resource Limits ===\n");

    task_queue_t *queue = task_queue_create(NULL);
    resource_limits_t *limits = resource_limits_create(NULL);

    if (!queue || !limits) {
        log_error("Failed to create queue or limits");
        return;
    }

    /* Enqueue tasks */
    for (int i = 0; i < 7; i++) {
        task_t task = {
            .task_id = i + 1,
            .model_id = 100,
            .api_version = API_VERSION_CURRENT,
            .flags = 0,
            .timeout_secs = 30
        };
        task_queue_enqueue(queue, &task);
    }

    printf("Enqueued 7 tasks\n");

    /* Process tasks with resource checking */
    int processed = 0;
    while (!task_queue_is_empty(queue)) {
        /* Check resources before taking task */
        limit_status_t status = resource_limits_check_all(limits);

        if (status >= LIMIT_EXCEEDED) {
            printf("Resource limit exceeded - pausing\n");
            sleep(1);
            resource_limits_update(limits);
            continue;
        }

        /* Reserve resources */
        if (resource_limits_reserve_task(limits) != 0) {
            printf("Cannot reserve resources - waiting\n");
            sleep(1);
            continue;
        }

        /* Dequeue task */
        task_t task;
        if (task_queue_dequeue(queue, &task) == 0) {
            printf("Processing task %lu\n", task.task_id);

            /* Simulate work */
            usleep(200000);

            /* Release resources */
            resource_limits_release_task(limits, 1);
            processed++;
        }
    }

    printf("\nProcessed %d tasks successfully\n", processed);

    /* Display final stats */
    task_queue_stats_t queue_stats;
    task_queue_get_stats(queue, &queue_stats);

    resource_stats_t resource_stats;
    resource_limits_get_stats(limits, &resource_stats);

    printf("\nQueue statistics:\n");
    printf("  Enqueued: %lu\n", queue_stats.total_enqueued);
    printf("  Dequeued: %lu\n", queue_stats.total_dequeued);

    printf("\nResource statistics:\n");
    printf("  Completed: %lu\n", resource_stats.tasks_completed);
    printf("  Failed: %lu\n", resource_stats.tasks_failed);
    printf("  Peak memory: %.1f MB\n", resource_stats.memory_peak_bytes / (1024.0 * 1024.0));

    task_queue_destroy(queue);
    resource_limits_destroy(limits);
}

int main(int argc, char *argv[])
{
    /* Initialise logging */
    if (log_init("runtime_example.log", LOG_LEVEL_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialise logging\n");
        return 1;
    }

    error_reporter_install(NULL, "runtime_example.log");

    log_info("Runtime module example started");

    /* Seed random number generator */
    srand(time(NULL));

    /* Run examples */
    example_task_queue_basic();
    example_producer_consumer();
    example_resource_limits();
    example_integrated();

    printf("\n=== All Examples Completed ===\n");
    printf("Check runtime_example.log for detailed logs\n");

    log_info("Runtime module example completed successfully");
    log_shutdown();

    return 0;
}