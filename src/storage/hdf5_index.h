#ifndef HDF5_INDEX_H
#define HDF5_INDEX_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Storage module: hdf5_index.h
 *
 * Provides indexing and organisation for HDF5 scientific data files.
 * This module creates a lightweight index over HDF5 files to enable
 * fast querying and retrieval without opening all files.
 *
 * Design:
 * - Each task result is stored in an HDF5 file
 * - An index tracks which files contain which tasks
 * - Supports queries by task ID, model, time range, etc.
 * - Index is separate from HDF5 files for fast lookups
 */

/* Maximum paths and names */
#define HDF5_MAX_PATH_LEN 4096
#define HDF5_MAX_NAME_LEN 256
#define HDF5_MAX_DATASET_NAME 256

/* HDF5 index handle (opaque) */
typedef struct hdf5_index hdf5_index_t;

/* Dataset type in HDF5 file */
typedef enum {
    HDF5_DATASET_UNKNOWN = 0,
    HDF5_DATASET_INPUT = 1,
    HDF5_DATASET_OUTPUT = 2,
    HDF5_DATASET_STATE = 3,
    HDF5_DATASET_CHECKPOINT = 4,
    HDF5_DATASET_METADATA = 5
} hdf5_dataset_type_t;

/* Dataset info structure */
typedef struct {
    char name[HDF5_MAX_DATASET_NAME];
    hdf5_dataset_type_t type;
    size_t rank;              /* Number of dimensions */
    size_t dims[8];           /* Dimension sizes (max 8 dimensions) */
    size_t element_size;      /* Size of each element in bytes */
    char dtype_name[64];      /* Human-readable type name */
} hdf5_dataset_info_t;

/* File entry in index */
typedef struct {
    char filepath[HDF5_MAX_PATH_LEN];
    uint64_t task_id;
    uint32_t model_id;
    time_t created;
    size_t file_size;
    int dataset_count;
    hdf5_dataset_info_t datasets[32];  /* Up to 32 datasets per file */
} hdf5_file_entry_t;

/* Query filter for HDF5 files */
typedef struct {
    uint64_t task_id;         /* 0 = match all */
    uint32_t model_id;        /* 0 = match all */
    time_t created_after;     /* 0 = no filter */
    time_t created_before;    /* 0 = no filter */
    const char *dataset_name; /* NULL = ignore */
} hdf5_query_t;

/*
 * Create or open an HDF5 index
 *
 * index_path: path to index file (SQLite database)
 * create: if non-zero, create index if it doesn't exist
 *
 * Returns: index handle on success, NULL on failure
 */
hdf5_index_t *hdf5_index_open(const char *index_path, int create);

/*
 * Close HDF5 index
 */
void hdf5_index_close(hdf5_index_t *idx);

/*
 * Add an HDF5 file to the index
 *
 * Scans the HDF5 file and adds metadata to index.
 *
 * idx: index handle
 * filepath: path to HDF5 file
 * task_id: task identifier
 * model_id: model identifier
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_index_add_file(hdf5_index_t *idx,
                        const char *filepath,
                        uint64_t task_id,
                        uint32_t model_id);

/*
 * Remove a file entry from index
 *
 * Does not delete the HDF5 file itself.
 */
int hdf5_index_remove_file(hdf5_index_t *idx, const char *filepath);

/*
 * Query index for matching files
 *
 * idx: index handle
 * query: filter criteria (NULL = match all)
 * results: output array of file entries (caller allocated)
 * max_results: maximum entries to return
 * count: output number of results found
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_index_query(hdf5_index_t *idx,
                     const hdf5_query_t *query,
                     hdf5_file_entry_t *results,
                     size_t max_results,
                     size_t *count);

/*
 * Get file entry by task ID
 *
 * Convenience function for single task lookup.
 */
int hdf5_index_get_by_task(hdf5_index_t *idx,
                           uint64_t task_id,
                           hdf5_file_entry_t *entry);

/*
 * Create a new HDF5 file for task output
 *
 * Creates an HDF5 file with standard structure for task results.
 * Automatically adds file to index.
 *
 * idx: index handle (may be NULL if not indexing)
 * output_dir: directory for HDF5 files
 * task_id: task identifier
 * model_id: model identifier
 * filepath_out: output buffer for created file path (caller allocated)
 * filepath_size: size of output buffer
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_create_task_file(hdf5_index_t *idx,
                          const char *output_dir,
                          uint64_t task_id,
                          uint32_t model_id,
                          char *filepath_out,
                          size_t filepath_size);

/*
 * Write dataset to HDF5 file
 *
 * filepath: path to HDF5 file
 * dataset_name: name of dataset
 * data: pointer to data buffer
 * rank: number of dimensions
 * dims: array of dimension sizes
 * element_size: size of each element in bytes
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_write_dataset(const char *filepath,
                       const char *dataset_name,
                       const void *data,
                       size_t rank,
                       const size_t *dims,
                       size_t element_size);

/*
 * Read dataset from HDF5 file
 *
 * filepath: path to HDF5 file
 * dataset_name: name of dataset
 * data_out: output buffer (caller allocated, must be large enough)
 * max_elements: maximum elements that fit in buffer
 * elements_read: output number of elements read
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_read_dataset(const char *filepath,
                      const char *dataset_name,
                      void *data_out,
                      size_t max_elements,
                      size_t *elements_read);

/*
 * Get information about a dataset
 *
 * filepath: path to HDF5 file
 * dataset_name: name of dataset
 * info: output structure for dataset info
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_get_dataset_info(const char *filepath,
                          const char *dataset_name,
                          hdf5_dataset_info_t *info);

/*
 * List all datasets in an HDF5 file
 *
 * filepath: path to HDF5 file
 * datasets: output array of dataset info (caller allocated)
 * max_datasets: maximum datasets to return
 * count: output number of datasets found
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_list_datasets(const char *filepath,
                       hdf5_dataset_info_t *datasets,
                       size_t max_datasets,
                       size_t *count);

/*
 * Store task result in HDF5 file
 *
 * High-level convenience function that creates file and writes datasets.
 *
 * idx: index handle (may be NULL)
 * output_dir: directory for output files
 * task_id: task identifier
 * model_id: model identifier
 * input_data: pointer to input data (may be NULL)
 * input_size: size of input data in bytes
 * output_data: pointer to output data
 * output_size: size of output data in bytes
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_store_task_result(hdf5_index_t *idx,
                           const char *output_dir,
                           uint64_t task_id,
                           uint32_t model_id,
                           const void *input_data,
                           size_t input_size,
                           const void *output_data,
                           size_t output_size);

/*
 * Rebuild index by scanning directory
 *
 * Scans a directory for HDF5 files and rebuilds the index.
 * Useful for recovering from index corruption or creating
 * an index for existing files.
 *
 * idx: index handle
 * directory: directory to scan
 *
 * Returns: 0 on success, error code on failure
 */
int hdf5_index_rebuild(hdf5_index_t *idx, const char *directory);

/*
 * Get index statistics
 *
 * Returns statistics about the index.
 */
typedef struct {
    size_t total_files;
    size_t total_size_bytes;
    size_t unique_models;
    time_t oldest_file;
    time_t newest_file;
} hdf5_index_stats_t;

int hdf5_index_get_stats(hdf5_index_t *idx, hdf5_index_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* HDF5_INDEX_H */