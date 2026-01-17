/*
 * Main program for distributed computing worker
 *
 * Usage:
 *   Standalone:      ./worker --zmq tcp://coordinator:5555
 *   MPI worker:      mpirun -n 4 ./worker --mpi
 *   Master-worker:   mpirun -n 4 ./worker --master --zmq tcp://coord:5555
 */

#include "worker.h"
#include "../common/log.h"
#include "../common/errors.h"
#include "../../include/project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>

#ifdef HAVE_MPI
#include <mpi.h>
#endif

static worker_t *g_worker = NULL;

/* Signal handler for graceful shutdown */
static void signal_handler(int sig)
{
    (void)sig;
    if (g_worker) {
        log_info("Received shutdown signal");
        worker_stop(g_worker);
    }
}

static void print_usage(const char *prog_name)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Options:\n"
            "  --zmq ENDPOINT           Connect to coordinator at ENDPOINT\n"
            "                           (e.g., tcp://192.168.1.100:5555)\n"
            "  --mpi                    Use MPI transport (requires mpirun)\n"
            "  --master                 Run as master-worker (orchestrates local MPI)\n"
            "  --max-memory MB          Maximum memory usage in MB (default: 4096)\n"
            "  --max-concurrent N       Maximum concurrent tasks (default: 1)\n"
            "  --heartbeat MS           Heartbeat interval in ms (default: 5000)\n"
            "  --work-dir DIR           Working directory (default: ./work)\n"
            "  --result-dir DIR         Results directory (default: ./results)\n"
            "  -h, --help               Show this help message\n"
            "\n"
            "Examples:\n"
            "  # Standalone worker connecting to coordinator\n"
            "  %s --zmq tcp://192.168.1.100:5555\n"
            "\n"
            "  # Local MPI worker (part of cluster)\n"
            "  mpirun -n 8 %s --mpi\n"
            "\n"
            "  # Master-worker: local MPI cluster + remote coordinator\n"
            "  mpirun -n 8 %s --master --zmq tcp://coordinator:5555\n"
            "\n"
            "  # Limited resources\n"
            "  %s --zmq tcp://coord:5555 --max-memory 2048 --max-concurrent 2\n",
            prog_name, prog_name, prog_name, prog_name, prog_name);
}

int main(int argc, char **argv)
{
    int mpi_rank = 0;
    int mpi_size = 1;
    
#ifdef HAVE_MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
#endif
    
    /* Default configuration */
    worker_config_t config = {
        .transport_type = TRANSPORT_TYPE_ZMQ,
        .coordinator_endpoint = "tcp://localhost:5555",
        .mpi_rank = mpi_rank,
        .mode = WORKER_MODE_STANDALONE,
        .num_sub_workers = 0,
        .local_transport = NULL,
        .resource_config = {
            .max_memory_bytes = 4096UL * 1024 * 1024,
            .warn_memory_bytes = 3072UL * 1024 * 1024,
            .max_cpu_time_us = 0,
            .max_cpu_percent = 90.0,
            .max_concurrent_tasks = 1,
            .min_disk_space_bytes = 10UL * 1024 * 1024 * 1024,
            .max_open_files = 1024,
            .enforce_hard_limits = 1
        },
        .heartbeat_interval_ms = 5000,
        .max_concurrent_tasks = 1,
        .allow_task_cancellation = 1,
        .work_dir = "./work",
        .result_dir = "./results"
    };
    
    int use_mpi = 0;
    int master_mode = 0;
    
    /* Parse command line arguments */
    static struct option long_options[] = {
        {"zmq",            required_argument, 0, 'z'},
        {"mpi",            no_argument,       0, 'm'},
        {"master",         no_argument,       0, 'M'},
        {"max-memory",     required_argument, 0, 'n'},
        {"max-concurrent", required_argument, 0, 'c'},
        {"heartbeat",      required_argument, 0, 'b'},
        {"work-dir",       required_argument, 0, 'w'},
        {"result-dir",     required_argument, 0, 'r'},
        {"help",           no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "hz:mmMn:c:b:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'z':
                config.transport_type = TRANSPORT_TYPE_ZMQ;
                config.coordinator_endpoint = optarg;
                break;
            case 'm':
                use_mpi = 1;
                config.transport_type = TRANSPORT_TYPE_MPI;
                config.mode = WORKER_MODE_LOCAL_MPI;
                break;
            case 'M':
                master_mode = 1;
                break;
            case 'n':
                config.resource_config.max_memory_bytes = atoi(optarg) * 1024UL * 1024;
                config.resource_config.warn_memory_bytes = config.resource_config.max_memory_bytes * 0.75;
                break;
            case 'c':
                config.max_concurrent_tasks = atoi(optarg);
                config.resource_config.max_concurrent_tasks = atoi(optarg);
                break;
            case 'b':
                config.heartbeat_interval_ms = atoi(optarg);
                break;
            case 'w':
                config.work_dir = optarg;
                break;
            case 'r':
                config.result_dir = optarg;
                break;
            case 'h':
                if (mpi_rank == 0) print_usage(argv[0]);
#ifdef HAVE_MPI
                MPI_Finalize();
#endif
                return 0;
            default:
                if (mpi_rank == 0) print_usage(argv[0]);
#ifdef HAVE_MPI
                MPI_Finalize();
#endif
                return 1;
        }
    }
    
    /* Configure master-worker mode */
    if (master_mode && mpi_size > 1) {
        if (mpi_rank == 0) {
            /* Master worker */
            config.mode = WORKER_MODE_MASTER;
            config.num_sub_workers = mpi_size - 1;
            
            /* Create local MPI transport for sub-workers */
#ifdef HAVE_MPI
            transport_config_t local_cfg = {
                .type = TRANSPORT_TYPE_MPI,
                .role = TRANSPORT_ROLE_COORDINATOR,
                .endpoint = NULL,
                .timeout_ms = 1000,
                .max_workers = mpi_size - 1
            };
            transport_init(&config.local_transport, &local_cfg);
#endif
        } else {
            /* Sub-worker - run simple worker loop */
            config.mode = WORKER_MODE_LOCAL_MPI;
            use_mpi = 1;
        }
    }
    
    /* Initialise logging */
    char log_file[256];
    snprintf(log_file, sizeof(log_file), "worker-%d.log", mpi_rank);
    
    if (log_init(log_file, LOG_LEVEL_INFO) != 0) {
        fprintf(stderr, "Failed to initialise logging\n");
#ifdef HAVE_MPI
        MPI_Finalize();
#endif
        return 1;
    }
    
    /* Install error reporter */
    error_reporter_install(NULL, log_file);
    
    log_info("=== Distributed Computing Worker Starting ===");
    log_info("Mode: %s",
             config.mode == WORKER_MODE_MASTER ? "master" :
             config.mode == WORKER_MODE_LOCAL_MPI ? "local-mpi" : "standalone");
    log_info("MPI rank: %d / %d", mpi_rank, mpi_size);
    log_info("Transport: %s", transport_type_to_string(config.transport_type));
    
    if (config.mode == WORKER_MODE_STANDALONE || config.mode == WORKER_MODE_MASTER) {
        log_info("Coordinator: %s", config.coordinator_endpoint);
    }
    
    if (config.mode == WORKER_MODE_MASTER) {
        log_info("Managing %d sub-workers", config.num_sub_workers);
    }
    
    /* Install signal handlers (only on rank 0 or standalone) */
    if (mpi_rank == 0 || config.mode == WORKER_MODE_STANDALONE) {
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
    }
    
    /* Create worker */
    g_worker = worker_create(&config);
    if (!g_worker) {
        log_fatal("Failed to create worker");
        log_shutdown();
#ifdef HAVE_MPI
        MPI_Finalize();
#endif
        return 1;
    }
    
    log_info("Worker created successfully");
    
    /* Run worker main loop */
    int rc = worker_run(g_worker);
    
    if (rc != 0) {
        log_error("Worker exited with error code %d", rc);
    } else {
        log_info("Worker shutdown complete");
    }
    
    /* Print final statistics */
    worker_stats_t stats;
    worker_get_stats(g_worker, &stats);
    
    log_info("=== Final Statistics ===");
    log_info("Tasks executed:  %lu", stats.tasks_executed);
    log_info("Tasks completed: %lu", stats.tasks_completed);
    log_info("Tasks failed:    %lu", stats.tasks_failed);
    log_info("Total exec time: %lu us (%.2f s)",
             stats.total_exec_time_us,
             stats.total_exec_time_us / 1000000.0);
    
    if (stats.tasks_completed > 0) {
        log_info("Avg exec time:   %.2f ms",
                stats.total_exec_time_us / (double)stats.tasks_completed / 1000.0);
    }
    
    /* Clean up */
    worker_destroy(g_worker);
    
    log_shutdown();
    
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    
    return (rc == 0) ? 0 : 1;
}