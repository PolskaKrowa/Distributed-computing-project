/*
 * Storage Module Example
 * 
 * Demonstrates usage of metadata store and HDF5 indexing
 * for the Distributed Computing Project.
 *
 * Compile:
 *   gcc -o storage_example storage_example.c \
 *       src/storage/metadata.c \
 *       src/storage/hdf5_index.c \
 *       src/common/log.c \
 *       -I./include -I./src \
 *       -lsqlite3 -lhdf5 -lm
 */

#include "storage/metadata.h"
#include "storage/hdf5_index.h"
#include "common/log.h"
#include "common/errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

/* Helper: create directory if it doesn't exist */
static void ensure_directory(const char *path)
{
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0755);
    }
}

/* Example 1: Basic metadata operations */
static void example_metadata_basic(void)
{
    printf("\n=== Example 1: Basic Metadata Operations ===\n");

    metadata_store_t *store = metadata_store_open("./example_metadata", 1);
    if (!store) {
        log_error("Failed to open metadata store");
        return;
    }

    /* Store various types of metadata */
    metadata_store_put_string(store, "project.name", 
                              "Harmonic Oscillator Study", "config");
    metadata_store_put_string(store, "project.author", 
                              "Research Team", "config");
    metadata_store_put_int64(store, "project.task_count", 100, "config");
    metadata_store_put_double(store, "project.version", 1.0, "config");

    /* Retrieve and display */
    char name[256], author[256];
    int64_t count;
    double version;

    metadata_store_get_string(store, "project.name", name, sizeof(name));
    metadata_store_get_string(store, "project.author", author, sizeof(author));
    metadata_store_get_int64(store, "project.task_count", &count);
    metadata_store_get_double(store, "project.version", &version);

    printf("Project: %s\n", name);
    printf("Author: %s\n", author);
    printf("Task count: %ld\n", count);
    printf("Version: %.1f\n", version);

    metadata_store_close(store);
}

/* Example 2: Task metadata */
static void example_metadata_tasks(void)
{
    printf("\n=== Example 2: Task Metadata ===\n");

    metadata_store_t *store = metadata_store_open("./example_metadata", 1);
    if (!store) {
        log_error("Failed to open metadata store");
        return;
    }

    /* Store metadata for several tasks */
    for (uint64_t task_id = 1; task_id <= 5; ++task_id) {
        metadata_store_task_info(store,
            task_id,                    // task_id
            100,                        // model_id
            0,                          // status (OK)
            1000 * task_id,            // exec_time_us
            500 * task_id,             // queue_time_us
            1                          // worker_id
        );
    }

    /* Query all task metadata */
    metadata_query_t query = {
        .key_prefix = "task.",
        .tag = "task",
        .type = -1
    };

    metadata_entry_t results[100];
    size_t count;
    metadata_store_query(store, &query, results, 100, &count);

    printf("Found %zu task metadata entries\n", count);
    for (size_t i = 0; i < count && i < 10; ++i) {
        printf("  - %s = %ld\n", results[i].key, results[i].value.i64);
    }

    metadata_store_close(store);
}

/* Example 3: Experiment configuration */
static void example_metadata_experiment(void)
{
    printf("\n=== Example 3: Experiment Configuration ===\n");

    metadata_store_t *store = metadata_store_open("./example_metadata", 1);
    if (!store) {
        log_error("Failed to open metadata store");
        return;
    }

    /* Store experiment configuration */
    const char *config_json = 
        "{"
        "  \"model\": \"harmonic_oscillator\","
        "  \"parameters\": {"
        "    \"omega\": 2.5,"
        "    \"damping\": 0.1,"
        "    \"amplitude\": 1.0"
        "  },"
        "  \"integration\": {"
        "    \"method\": \"rk4\","
        "    \"dt\": 0.01,"
        "    \"steps\": 10000"
        "  }"
        "}";

    metadata_store_experiment(store,
        "exp_001",
        "Parameter sweep for damped harmonic oscillator",
        "harmonic_oscillator",
        config_json
    );

    /* Retrieve experiment info */
    char description[1024], model[256], config[4096];
    metadata_store_get_string(store, "experiment.exp_001.description", 
                              description, sizeof(description));
    metadata_store_get_string(store, "experiment.exp_001.model", 
                              model, sizeof(model));
    metadata_store_get_string(store, "experiment.exp_001.config", 
                              config, sizeof(config));

    printf("Experiment: exp_001\n");
    printf("Description: %s\n", description);
    printf("Model: %s\n", model);
    printf("Configuration:\n%s\n", config);

    metadata_store_close(store);
}

/* Example 4: HDF5 basic operations */
static void example_hdf5_basic(void)
{
    printf("\n=== Example 4: HDF5 Basic Operations ===\n");

    ensure_directory("./example_data");

    hdf5_index_t *idx = hdf5_index_open("./example_data/index.db", 1);
    if (!idx) {
        log_error("Failed to open HDF5 index");
        return;
    }

    /* Generate some sample data (sine wave) */
    const int n_points = 1000;
    double *input_data = malloc(n_points * sizeof(double));
    double *output_data = malloc(n_points * sizeof(double));

    for (int i = 0; i < n_points; ++i) {
        double t = i * 0.01;
        input_data[i] = t;
        output_data[i] = sin(2.0 * M_PI * t);
    }

    /* Store task result */
    int rc = hdf5_store_task_result(idx,
        "./example_data",
        12345,                              // task_id
        100,                                // model_id
        input_data, n_points * sizeof(double),
        output_data, n_points * sizeof(double)
    );

    if (rc == 0) {
        printf("Stored task 12345 result in HDF5\n");
    }

    /* Retrieve file info */
    hdf5_file_entry_t entry;
    if (hdf5_index_get_by_task(idx, 12345, &entry) == 0) {
        printf("File: %s\n", entry.filepath);
        printf("Size: %zu bytes\n", entry.file_size);
        printf("Model: %u\n", entry.model_id);
        printf("Datasets: %d\n", entry.dataset_count);
    }

    free(input_data);
    free(output_data);
    hdf5_index_close(idx);
}

/* Example 5: Multiple tasks and queries */
static void example_hdf5_multiple_tasks(void)
{
    printf("\n=== Example 5: Multiple Tasks and Queries ===\n");

    ensure_directory("./example_data");

    hdf5_index_t *idx = hdf5_index_open("./example_data/index.db", 1);
    if (!idx) {
        log_error("Failed to open HDF5 index");
        return;
    }

    /* Store results for multiple tasks */
    const int n_tasks = 10;
    const int n_points = 100;
    double *data = malloc(n_points * sizeof(double));

    for (int task = 0; task < n_tasks; ++task) {
        uint64_t task_id = 1000 + task;
        uint32_t model_id = 100 + (task % 3);  // Models 100, 101, 102

        /* Generate dummy data */
        for (int i = 0; i < n_points; ++i) {
            data[i] = task * 10.0 + i * 0.1;
        }

        hdf5_store_task_result(idx,
            "./example_data",
            task_id,
            model_id,
            data, n_points * sizeof(double),
            data, n_points * sizeof(double)
        );
    }

    printf("Stored %d task results\n", n_tasks);

    /* Query by model ID */
    hdf5_query_t query = {
        .task_id = 0,
        .model_id = 101  // Only model 101
    };

    hdf5_file_entry_t results[100];
    size_t count;
    hdf5_index_query(idx, &query, results, 100, &count);

    printf("\nTasks for model 101: %zu\n", count);
    for (size_t i = 0; i < count; ++i) {
        printf("  - Task %lu: %s (%zu bytes)\n",
               results[i].task_id,
               results[i].filepath,
               results[i].file_size);
    }

    /* Get index statistics */
    hdf5_index_stats_t stats;
    hdf5_index_get_stats(idx, &stats);

    printf("\nIndex Statistics:\n");
    printf("  Total files: %zu\n", stats.total_files);
    printf("  Total size: %zu bytes (%.2f MB)\n", 
           stats.total_size_bytes,
           stats.total_size_bytes / (1024.0 * 1024.0));
    printf("  Unique models: %zu\n", stats.unique_models);

    free(data);
    hdf5_index_close(idx);
}

/* Example 6: Custom HDF5 datasets */
static void example_hdf5_custom_datasets(void)
{
    printf("\n=== Example 6: Custom HDF5 Datasets ===\n");

    ensure_directory("./example_data");

    hdf5_index_t *idx = hdf5_index_open("./example_data/index.db", 1);
    if (!idx) {
        log_error("Failed to open HDF5 index");
        return;
    }

    /* Create a task file */
    char filepath[4096];
    hdf5_create_task_file(idx, "./example_data", 99999, 200,
                          filepath, sizeof(filepath));

    printf("Created file: %s\n", filepath);

    /* Write a 2D matrix */
    double matrix[10][10];
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            matrix[i][j] = i * 10 + j;
        }
    }

    size_t dims[2] = {10, 10};
    hdf5_write_dataset(filepath, "/output/matrix", 
                       matrix, 2, dims, sizeof(double));

    printf("Wrote 10x10 matrix to /output/matrix\n");

    /* Write a 1D vector */
    double vector[50];
    for (int i = 0; i < 50; ++i) {
        vector[i] = sqrt((double)i);
    }

    size_t dims1d[1] = {50};
    hdf5_write_dataset(filepath, "/output/vector", 
                       vector, 1, dims1d, sizeof(double));

    printf("Wrote 50-element vector to /output/vector\n");

    /* Read back and verify */
    double read_vector[50];
    size_t elements_read;
    hdf5_read_dataset(filepath, "/output/vector", 
                      read_vector, 50, &elements_read);

    printf("Read back %zu elements\n", elements_read);
    printf("First few values: %.3f, %.3f, %.3f, %.3f\n",
           read_vector[0], read_vector[1], read_vector[2], read_vector[3]);

    hdf5_index_close(idx);
}

/* Main programme */
int main(int argc, char *argv[])
{
    /* Initialise logging */
    if (log_init("storage_example.log", LOG_LEVEL_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialise logging\n");
        return 1;
    }

    /* Install error reporter */
    error_reporter_install(NULL, "storage_example.log");

    log_info("Storage module example started");

    /* Ensure directories exist */
    ensure_directory("./example_metadata");
    ensure_directory("./example_data");

    /* Run examples */
    example_metadata_basic();
    example_metadata_tasks();
    example_metadata_experiment();
    example_hdf5_basic();
    example_hdf5_multiple_tasks();
    example_hdf5_custom_datasets();

    printf("\n=== All Examples Completed ===\n");
    printf("Check the following directories:\n");
    printf("  - ./example_metadata/     (metadata files)\n");
    printf("  - ./example_data/         (HDF5 files and index)\n");
    printf("  - storage_example.log     (log file)\n");

    log_info("Storage module example completed successfully");
    log_shutdown();

    return 0;
}