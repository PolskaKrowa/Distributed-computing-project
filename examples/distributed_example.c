/*
 * Complete example showing distributed computing usage
 *
 * This example demonstrates:
 * 1. Creating and configuring a coordinator
 * 2. Submitting tasks in batches
 * 3. Monitoring progress
 * 4. Collecting results
 */

#include "../src/coordinator/coordinator.h"
#include "../src/common/log.h"
#include "../include/project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* Example: Submit a batch of physics simulation tasks */
static int submit_simulation_tasks(coordinator_t *coord, int num_tasks)
{
    log_info("Submitting %d simulation tasks", num_tasks);
    
    for (int i = 0; i < num_tasks; ++i) {
        task_t task = {0};
        
        /* Set task parameters */
        task.task_id = i + 1;
        task.model_id = 1;  /* Physics model */
        task.api_version = API_VERSION_CURRENT;
        
        /* Allocate input data */
        double input_data[100];
        for (int j = 0; j < 100; ++j) {
            input_data[j] = (double)i * 0.1 + j;
        }
        
        task.input = input_data;
        task.input_size = sizeof(input_data);
        
        /* Allocate output buffer */
        static double output_buffer[200];
        task.output = output_buffer;
        task.output_size = sizeof(output_buffer);
        
        /* Set trace ID for debugging */
        task.trace_id = task.task_id;
        
        /* Submit task */
        int rc = coordinator_submit_task(coord, &task);
        if (rc != 0) {
            log_error("Failed to submit task %d: error %d", i, rc);
            return -1;
        }
        
        /* Add small delay to avoid overwhelming queue */
        if (i % 100 == 0) {
            usleep(1000);
        }
    }
    
    log_info("Successfully submitted %d tasks", num_tasks);
    return 0;
}

/* Monitor coordinator progress */
static void *monitor_thread(void *arg)
{
    coordinator_t *coord = (coordinator_t *)arg;
    
    while (1) {
        sleep(5);
        
        coordinator_stats_t stats;
        coordinator_get_stats(coord, &stats);
        
        uint32_t workers = coordinator_worker_count(coord);
        
        log_info("Progress: %lu/%lu completed, %lu failed, %u workers active",
                stats.tasks_completed,
                stats.tasks_submitted,
                stats.tasks_failed,
                workers);
        
        if (stats.tasks_completed + stats.tasks_failed >= stats.tasks_submitted) {
            break;
        }
    }
    
    return NULL;
}

int main(int argc, char **argv)
{
    /* Initialise logging */
    if (log_init("example.log", LOG_LEVEL_INFO) != 0) {
        fprintf(stderr, "Failed to initialise logging\n");
        return 1;
    }
    
    log_info("=== Distributed Computing Example ===");
    
    /* Configure coordinator */
    coordinator_config_t config = {
        .transport_type = TRANSPORT_TYPE_ZMQ,
        .endpoint = "tcp://*:5555",
        .use_mpi = 0,
        .task_queue_size = 10000,
        .allow_task_preemption = 1,
        .max_workers = 100,
        .worker_timeout_ms = 30000,
        .task_timeout_ms = 60000,
        .batch_size = 10,           /* Batch 10 tasks for network efficiency */
        .batch_timeout_ms = 500,
        .result_dir = "./results",
        .metadata_dir = "./metadata",
        .checkpoint_interval_s = 300
    };
    
    /* Create coordinator */
    log_info("Creating coordinator...");
    coordinator_t *coord = coordinator_create(&config);
    if (!coord) {
        log_error("Failed to create coordinator");
        log_shutdown();
        return 1;
    }
    
    log_info("Coordinator created successfully");
    log_info("Listening on %s", config.endpoint);
    log_info("Waiting for workers to connect...");
    
    /* Start coordinator in background thread */
    pthread_t coord_thread;
    pthread_create(&coord_thread, NULL, (void *(*)(void *))coordinator_run, coord);
    
    /* Wait for at least one worker */
    log_info("Waiting for workers...");
    while (coordinator_worker_count(coord) == 0) {
        sleep(1);
    }
    
    log_info("Workers connected: %u", coordinator_worker_count(coord));
    
    /* Start monitoring thread */
    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_thread, coord);
    
    /* Submit tasks */
    int num_tasks = 1000;
    if (argc > 1) {
        num_tasks = atoi(argv[1]);
    }
    
    log_info("Submitting %d tasks...", num_tasks);
    
    if (submit_simulation_tasks(coord, num_tasks) != 0) {
        log_error("Failed to submit tasks");
        coordinator_stop(coord);
        pthread_join(coord_thread, NULL);
        coordinator_destroy(coord);
        log_shutdown();
        return 1;
    }
    
    log_info("All tasks submitted, waiting for completion...");
    
    /* Wait for all tasks to complete */
    int rc = coordinator_wait_all(coord, -1);
    
    if (rc == 0) {
        log_info("All tasks completed successfully!");
    } else {
        log_warn("Some tasks may not have completed");
    }
    
    /* Get final statistics */
    coordinator_stats_t stats;
    coordinator_get_stats(coord, &stats);
    
    printf("\n=== Final Results ===\n");
    printf("Tasks submitted:  %lu\n", stats.tasks_submitted);
    printf("Tasks completed:  %lu\n", stats.tasks_completed);
    printf("Tasks failed:     %lu\n", stats.tasks_failed);
    printf("Workers (total):  %u\n", stats.workers_total);
    printf("Avg task time:    %.2f ms\n", stats.avg_task_time_ms);
    printf("Success rate:     %.1f%%\n",
           100.0 * stats.tasks_completed / (stats.tasks_completed + stats.tasks_failed));
    
    /* Shutdown */
    log_info("Shutting down coordinator...");
    coordinator_stop(coord);
    
    pthread_join(monitor, NULL);
    pthread_join(coord_thread, NULL);
    
    coordinator_destroy(coord);
    
    log_info("Example complete");
    log_shutdown();
    
    return 0;
}