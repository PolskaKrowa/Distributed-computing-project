/*
 * Main program for distributed computing coordinator
 *
 * Usage:
 *   MPI mode:     mpirun -n 1 ./coordinator --mpi
 *   ZMQ mode:     ./coordinator --zmq tcp://*:5555
 *   Mixed mode:   ./coordinator --zmq tcp://*:5555 --batch-size 10
 */

#include "coordinator.h"
#include "../common/log.h"
#include "../common/errors.h"
#include "../../include/project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

static coordinator_t *g_coordinator = NULL;

/* Signal handler for graceful shutdown */
static void signal_handler(int sig)
{
    (void)sig;
    if (g_coordinator) {
        log_info("Received shutdown signal");
        coordinator_stop(g_coordinator);
    }
}

static void print_usage(const char *prog_name)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Options:\n"
            "  --mpi                    Use MPI transport (requires mpirun)\n"
            "  --zmq ENDPOINT           Use ZeroMQ transport at ENDPOINT\n"
            "                           (e.g., tcp://*:5555)\n"
            "  --max-workers N          Maximum concurrent workers (default: 1024)\n"
            "  --queue-size N           Task queue size (default: 65536)\n"
            "  --batch-size N           Tasks per network batch (default: 1)\n"
            "  --batch-timeout MS       Batch accumulation timeout (default: 100)\n"
            "  --worker-timeout MS      Worker heartbeat timeout (default: 30000)\n"
            "  --task-timeout MS        Per-task timeout (default: 300000)\n"
            "  --result-dir DIR         Directory for results (default: ./results)\n"
            "  --metadata-dir DIR       Directory for metadata (default: ./metadata)\n"
            "  --checkpoint-interval S  Checkpoint interval in seconds (default: 300)\n"
            "  -h, --help               Show this help message\n"
            "\n"
            "Examples:\n"
            "  # Local MPI cluster\n"
            "  mpirun -n 1 %s --mpi\n"
            "\n"
            "  # Internet-based with batching\n"
            "  %s --zmq tcp://*:5555 --batch-size 10 --batch-timeout 500\n"
            "\n"
            "  # Mixed mode (local + remote workers)\n"
            "  %s --zmq tcp://*:5555 --max-workers 100\n",
            prog_name, prog_name, prog_name, prog_name);
}

int main(int argc, char **argv)
{
    /* Default configuration */
    coordinator_config_t config = {
        .transport_type = TRANSPORT_TYPE_ZMQ,
        .endpoint = "tcp://*:5555",
        .use_mpi = 0,
        .task_queue_size = 65536,
        .allow_task_preemption = 1,
        .max_workers = 1024,
        .worker_timeout_ms = 30000,
        .task_timeout_ms = 300000,
        .batch_size = 1,
        .batch_timeout_ms = 100,
        .result_dir = "./results",
        .metadata_dir = "./metadata",
        .checkpoint_interval_s = 300
    };
    
    /* Parse command line arguments */
    static struct option long_options[] = {
        {"mpi",                 no_argument,       0, 'm'},
        {"zmq",                 required_argument, 0, 'z'},
        {"max-workers",         required_argument, 0, 'w'},
        {"queue-size",          required_argument, 0, 'q'},
        {"batch-size",          required_argument, 0, 'b'},
        {"batch-timeout",       required_argument, 0, 't'},
        {"worker-timeout",      required_argument, 0, 'W'},
        {"task-timeout",        required_argument, 0, 'T'},
        {"result-dir",          required_argument, 0, 'r'},
        {"metadata-dir",        required_argument, 0, 'd'},
        {"checkpoint-interval", required_argument, 0, 'c'},
        {"help",                no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "hz:mw:q:b:t:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'm':
                config.transport_type = TRANSPORT_TYPE_MPI;
                config.use_mpi = 1;
                break;
            case 'z':
                config.transport_type = TRANSPORT_TYPE_ZMQ;
                config.endpoint = optarg;
                break;
            case 'w':
                config.max_workers = atoi(optarg);
                break;
            case 'q':
                config.task_queue_size = atoi(optarg);
                break;
            case 'b':
                config.batch_size = atoi(optarg);
                break;
            case 't':
                config.batch_timeout_ms = atoi(optarg);
                break;
            case 'W':
                config.worker_timeout_ms = atoi(optarg);
                break;
            case 'T':
                config.task_timeout_ms = atoi(optarg);
                break;
            case 'r':
                config.result_dir = optarg;
                break;
            case 'd':
                config.metadata_dir = optarg;
                break;
            case 'c':
                config.checkpoint_interval_s = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* Initialise logging */
    if (log_init("coordinator.log", LOG_LEVEL_INFO) != 0) {
        fprintf(stderr, "Failed to initialise logging\n");
        return 1;
    }
    
    /* Install error reporter */
    error_reporter_install("https://crash.example.com/upload", "coordinator.log");
    
    log_info("=== Distributed Computing Coordinator Starting ===");
    log_info("Transport: %s", transport_type_to_string(config.transport_type));
    if (config.transport_type == TRANSPORT_TYPE_ZMQ) {
        log_info("Endpoint: %s", config.endpoint);
    }
    log_info("Max workers: %u", config.max_workers);
    log_info("Batch size: %zu", config.batch_size);
    
    /* Install signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create coordinator */
    g_coordinator = coordinator_create(&config);
    if (!g_coordinator) {
        log_fatal("Failed to create coordinator");
        log_shutdown();
        return 1;
    }
    
    log_info("Coordinator created successfully");
    log_info("Waiting for workers to connect...");
    
    /* Run coordinator main loop */
    int rc = coordinator_run(g_coordinator);
    
    if (rc != 0) {
        log_error("Coordinator exited with error code %d", rc);
    } else {
        log_info("Coordinator shutdown complete");
    }
    
    /* Print final statistics */
    coordinator_stats_t stats;
    coordinator_get_stats(g_coordinator, &stats);
    
    log_info("=== Final Statistics ===");
    log_info("Tasks submitted:  %lu", stats.tasks_submitted);
    log_info("Tasks completed:  %lu", stats.tasks_completed);
    log_info("Tasks failed:     %lu", stats.tasks_failed);
    log_info("Tasks cancelled:  %lu", stats.tasks_cancelled);
    log_info("Workers (total):  %u", stats.workers_total);
    log_info("Avg task time:    %.2f ms", stats.avg_task_time_ms);
    
    /* Clean up */
    coordinator_destroy(g_coordinator);
    
    log_shutdown();
    return (rc == 0) ? 0 : 1;
}