# Storage Module Documentation

The storage module provides metadata management and HDF5 file indexing for the Distributed Computing Project.

## Overview

The storage module consists of two main components:

1. **Metadata Store** (`metadata.h/c`) - Key-value storage for task and experiment metadata
2. **HDF5 Index** (`hdf5_index.h/c`) - Indexing and organisation for scientific data files

Both components are designed to be:
- Lightweight and fast
- Robust against corruption
- Easy to query and search
- Suitable for long-term storage

## Building

The storage module requires:
- SQLite3 (for metadata index)
- HDF5 library (for scientific data)

Add to your CMakeLists.txt:

```cmake
find_package(SQLite3 REQUIRED)
find_package(HDF5 REQUIRED COMPONENTS C)

target_link_libraries(your_target
    ${SQLITE3_LIBRARIES}
    ${HDF5_LIBRARIES}
)
```

## Metadata Store

### Basic Usage

```c
#include "storage/metadata.h"
#include "common/log.h"

int main(void) {
    log_init("app.log", LOG_LEVEL_DEBUG);
    
    // Open or create metadata store
    metadata_store_t *store = metadata_store_open("./metadata", 1);
    if (!store) {
        log_error("Failed to open metadata store");
        return 1;
    }
    
    // Store string metadata
    metadata_store_put_string(store, "experiment.name", 
                              "Harmonic Oscillator Study", 
                              "experiment");
    
    // Store numeric metadata
    metadata_store_put_int64(store, "task.count", 1000, "task");
    metadata_store_put_double(store, "runtime.seconds", 123.45, "runtime");
    
    // Retrieve metadata
    char name[256];
    metadata_store_get_string(store, "experiment.name", name, sizeof(name));
    log_info("Experiment: %s", name);
    
    int64_t count;
    metadata_store_get_int64(store, "task.count", &count);
    log_info("Task count: %ld", count);
    
    // Query metadata by tag
    metadata_query_t query = {
        .tag = "experiment",
        .type = -1  // Match all types
    };
    
    metadata_entry_t results[100];
    size_t result_count;
    metadata_store_query(store, &query, results, 100, &result_count);
    log_info("Found %zu experiment metadata entries", result_count);
    
    metadata_store_close(store);
    log_shutdown();
    return 0;
}
```

### Storing Task Information

```c
// Store comprehensive task metadata
metadata_store_task_info(store,
    task_id,        // uint64_t
    model_id,       // uint32_t
    status,         // int (error code)
    exec_time_us,   // uint64_t
    queue_time_us,  // uint64_t
    worker_id       // uint32_t
);
```

### Storing Experiment Configuration

```c
// Store experiment-level metadata
metadata_store_experiment(store,
    "exp_001",                           // experiment ID
    "Parameter sweep for oscillator",   // description
    "harmonic_oscillator",              // model name
    "{\"omega\": 2.5, \"damping\": 0.1}" // JSON config
);
```

## HDF5 Index

### Basic Usage

```c
#include "storage/hdf5_index.h"
#include "common/log.h"

int main(void) {
    log_init("app.log", LOG_LEVEL_DEBUG);
    
    // Open or create HDF5 index
    hdf5_index_t *idx = hdf5_index_open("./data/index.db", 1);
    if (!idx) {
        log_error("Failed to open HDF5 index");
        return 1;
    }
    
    // Store task result in HDF5 file
    double input_data[100];
    double output_data[200];
    // ... populate data ...
    
    hdf5_store_task_result(idx,
        "./data",                      // output directory
        12345,                         // task ID
        100,                          // model ID
        input_data, sizeof(input_data),
        output_data, sizeof(output_data)
    );
    
    // Query for files
    hdf5_query_t query = {
        .task_id = 0,      // 0 = match all
        .model_id = 100    // Only model 100
    };
    
    hdf5_file_entry_t results[100];
    size_t count;
    hdf5_index_query(idx, &query, results, 100, &count);
    
    log_info("Found %zu HDF5 files for model 100", count);
    
    // Get specific task file
    hdf5_file_entry_t entry;
    if (hdf5_index_get_by_task(idx, 12345, &entry) == 0) {
        log_info("Task 12345 stored in: %s", entry.filepath);
        log_info("File size: %zu bytes", entry.file_size);
        log_info("Datasets: %d", entry.dataset_count);
    }
    
    // Get index statistics
    hdf5_index_stats_t stats;
    hdf5_index_get_stats(idx, &stats);
    log_info("Total files: %zu", stats.total_files);
    log_info("Total size: %zu bytes", stats.total_size_bytes);
    log_info("Unique models: %zu", stats.unique_models);
    
    hdf5_index_close(idx);
    log_shutdown();
    return 0;
}
```

### Manual HDF5 Operations

```c
// Create a task file manually
char filepath[4096];
hdf5_create_task_file(idx, "./data", task_id, model_id, 
                      filepath, sizeof(filepath));

// Write custom datasets
double matrix[10][10];
size_t dims[2] = {10, 10};
hdf5_write_dataset(filepath, "/output/matrix", 
                   matrix, 2, dims, sizeof(double));

// Read datasets back
double result[100];
size_t elements_read;
hdf5_read_dataset(filepath, "/output/matrix", 
                  result, 100, &elements_read);

// Get dataset information
hdf5_dataset_info_t info;
hdf5_get_dataset_info(filepath, "/output/matrix", &info);
log_info("Dataset rank: %zu", info.rank);
log_info("Dimensions: %zu x %zu", info.dims[0], info.dims[1]);
```

## Integration with Worker

Example of integrating storage into a worker process:

```c
#include "storage/metadata.h"
#include "storage/hdf5_index.h"
#include "include/task.h"
#include "include/fortran_api.h"

int execute_and_store_task(task_t *task) {
    // Open storage
    metadata_store_t *meta = metadata_store_open("./metadata", 1);
    hdf5_index_t *idx = hdf5_index_open("./data/index.db", 1);
    
    // Record task start time
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Execute Fortran kernel
    int32_t status = fortran_model_run_v1(
        task->input, task->input_size,
        task->output, task->output_size,
        NULL, 0,
        task->trace_id,
        NULL
    );
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t exec_time_us = (end.tv_sec - start.tv_sec) * 1000000UL +
                           (end.tv_nsec - start.tv_nsec) / 1000UL;
    
    // Store results in HDF5
    hdf5_store_task_result(idx,
        "./data",
        task->task_id,
        task->model_id,
        task->input, task->input_size,
        task->output, task->output_size
    );
    
    // Store task metadata
    metadata_store_task_info(meta,
        task->task_id,
        task->model_id,
        status,
        exec_time_us,
        0,  // queue_time_us
        0   // worker_id
    );
    
    // Cleanup
    hdf5_index_close(idx);
    metadata_store_close(meta);
    
    return status;
}
```

## File Organization

### Metadata Store Layout

The metadata store is a directory containing individual `.meta` files:

```
metadata/
├── experiment.name.meta
├── experiment.model.meta
├── task.12345.status.meta
├── task.12345.exec_time_us.meta
└── ...
```

Each file contains:
- Magic number and version
- Key, type, value
- Tags for categorisation
- Creation and modification timestamps

### HDF5 File Layout

Task result files follow this structure:

```
task_12345_model_100.h5
├── /input/
│   └── data [dataset]
├── /output/
│   └── data [dataset]
├── /state/       (optional)
└── /checkpoint/  (optional)
```

The HDF5 index is a SQLite database tracking:
- File paths
- Task and model IDs
- File sizes and creation times
- Dataset names and dimensions

## Error Handling

All storage functions return:
- `0` on success
- Positive error codes on failure

Common error codes:
- `META_ERR_INVALID_ARG` / `HDF5_ERR_INVALID_ARG` - Invalid arguments
- `META_ERR_NOT_FOUND` / `HDF5_ERR_NOT_FOUND` - Entry not found
- `META_ERR_IO` / `HDF5_ERR_IO` - I/O error
- `HDF5_ERR_HDF5` - HDF5 library error
- `HDF5_ERR_DB` - SQLite database error

Always check return codes:

```c
int rc = metadata_store_put_string(store, key, value, tag);
if (rc != 0) {
    log_error("Failed to store metadata: error %d", rc);
    // Handle error
}
```

## Thread Safety

- **Metadata Store**: Not thread-safe. Use separate stores per thread or add external locking.
- **HDF5 Index**: SQLite provides some concurrency, but use locking for write operations.

For multi-threaded workers, create one store per thread:

```c
// Per-thread metadata store
__thread metadata_store_t *thread_meta_store = NULL;

void worker_thread_init(void) {
    char path[256];
    snprintf(path, sizeof(path), "./metadata/thread_%ld", 
             (long)pthread_self());
    thread_meta_store = metadata_store_open(path, 1);
}
```

## Performance Considerations

### Metadata Store

- **Fast for individual lookups** - O(1) file access
- **Slow for large queries** - O(n) directory scan
- **Batching**: Store related metadata together
- **Cleanup**: Periodically archive old metadata

### HDF5 Index

- **Fast queries** - SQLite B-tree indices
- **Batch inserts**: Use transactions for multiple files
- **Index rebuilding**: Run periodically to catch any drift

```c
// Use transactions for batch operations
sqlite3_exec(idx->db, "BEGIN TRANSACTION", NULL, NULL, NULL);
for (int i = 0; i < n_files; ++i) {
    hdf5_index_add_file(idx, files[i], task_ids[i], model_ids[i]);
}
sqlite3_exec(idx->db, "COMMIT", NULL, NULL, NULL);
```

## Maintenance

### Metadata Store Cleanup

```bash
# Remove old metadata (older than 30 days)
find ./metadata -name "*.meta" -mtime +30 -delete
```

### HDF5 Index Rebuild

```c
// Rebuild index from directory
hdf5_index_t *idx = hdf5_index_open("./data/index.db", 1);
hdf5_index_rebuild(idx, "./data");
hdf5_index_close(idx);
```

### Backup

```bash
# Backup metadata
tar czf metadata_backup_$(date +%Y%m%d).tar.gz ./metadata/

# Backup HDF5 index
sqlite3 ./data/index.db ".backup ./data/index_backup.db"

# Backup HDF5 data files
rsync -av --progress ./data/*.h5 /backup/location/
```

## Testing

Basic test to verify storage functionality:

```c
#include "storage/metadata.h"
#include "storage/hdf5_index.h"
#include <assert.h>

int main(void) {
    // Test metadata store
    metadata_store_t *meta = metadata_store_open("./test_meta", 1);
    assert(meta != NULL);
    
    assert(metadata_store_put_string(meta, "test.key", "test_value", "test") == 0);
    
    char value[256];
    assert(metadata_store_get_string(meta, "test.key", value, sizeof(value)) == 0);
    assert(strcmp(value, "test_value") == 0);
    
    metadata_store_close(meta);
    
    // Test HDF5 index
    hdf5_index_t *idx = hdf5_index_open("./test_index.db", 1);
    assert(idx != NULL);
    
    char filepath[4096];
    assert(hdf5_create_task_file(idx, "./test_data", 1, 100, 
                                 filepath, sizeof(filepath)) == 0);
    
    hdf5_file_entry_t entry;
    assert(hdf5_index_get_by_task(idx, 1, &entry) == 0);
    assert(entry.task_id == 1);
    assert(entry.model_id == 100);
    
    hdf5_index_close(idx);
    
    printf("All storage tests passed!\n");
    return 0;
}
```

## Further Reading

- See `docs/INCLUDE_FILES.md` for complete API documentation
- See `docs/DESIGN.md` for architecture overview
- HDF5 documentation: https://portal.hdfgroup.org/display/HDF5/HDF5
- SQLite documentation: https://www.sqlite.org/docs.html