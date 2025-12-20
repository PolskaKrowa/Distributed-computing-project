#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Storage module: metadata.h
 *
 * Provides metadata storage and retrieval for tasks, experiments,
 * and computational runs. Metadata is stored in a simple key-value
 * format with support for indexing and querying.
 *
 * This module handles:
 * - Task metadata (parameters, timestamps, status)
 * - Experiment configuration
 * - Run metadata (hardware info, versions)
 * - Search and retrieval operations
 */

/* Maximum lengths for metadata fields */
#define META_MAX_KEY_LEN 256
#define META_MAX_VALUE_LEN 4096
#define META_MAX_TAG_LEN 64
#define META_MAX_TAGS 32

/* Metadata value types */
typedef enum {
    META_TYPE_STRING = 0,
    META_TYPE_INT64 = 1,
    META_TYPE_DOUBLE = 2,
    META_TYPE_BLOB = 3,
    META_TYPE_TIMESTAMP = 4
} meta_value_type_t;

/* Metadata entry structure */
typedef struct {
    char key[META_MAX_KEY_LEN];
    meta_value_type_t type;
    union {
        char string[META_MAX_VALUE_LEN];
        int64_t i64;
        double f64;
        struct {
            void *data;
            size_t size;
        } blob;
        time_t timestamp;
    } value;
    char tags[META_MAX_TAGS][META_MAX_TAG_LEN];
    int tag_count;
    time_t created;
    time_t modified;
} metadata_entry_t;

/* Metadata store handle (opaque) */
typedef struct metadata_store metadata_store_t;

/* Query filter for metadata search */
typedef struct {
    const char *key_prefix;        /* NULL = match all */
    const char *tag;               /* NULL = ignore tags */
    meta_value_type_t type;        /* Used only if >= 0 */
    time_t created_after;          /* 0 = no filter */
    time_t created_before;         /* 0 = no filter */
} metadata_query_t;

/*
 * Initialise a metadata store
 *
 * path: directory path for metadata storage
 * create: if non-zero, create directory if it doesn't exist
 *
 * Returns: store handle on success, NULL on failure
 */
metadata_store_t *metadata_store_open(const char *path, int create);

/*
 * Close metadata store and flush pending writes
 *
 * store: store handle from metadata_store_open
 */
void metadata_store_close(metadata_store_t *store);

/*
 * Store a metadata entry
 *
 * store: store handle
 * entry: metadata entry to store
 *
 * Returns: 0 on success, error code on failure
 */
int metadata_store_put(metadata_store_t *store, const metadata_entry_t *entry);

/*
 * Retrieve a metadata entry by key
 *
 * store: store handle
 * key: metadata key
 * entry: output buffer for entry (caller allocated)
 *
 * Returns: 0 on success, error code if not found
 */
int metadata_store_get(metadata_store_t *store, const char *key, metadata_entry_t *entry);

/*
 * Delete a metadata entry
 *
 * store: store handle
 * key: metadata key to delete
 *
 * Returns: 0 on success, error code on failure
 */
int metadata_store_delete(metadata_store_t *store, const char *key);

/*
 * Query metadata entries matching filter
 *
 * store: store handle
 * query: filter criteria (NULL = match all)
 * results: output array of entries (caller allocated)
 * max_results: maximum entries to return
 * count: output number of results found
 *
 * Returns: 0 on success, error code on failure
 */
int metadata_store_query(metadata_store_t *store,
                         const metadata_query_t *query,
                         metadata_entry_t *results,
                         size_t max_results,
                         size_t *count);

/*
 * Helper: store string metadata
 */
int metadata_store_put_string(metadata_store_t *store,
                              const char *key,
                              const char *value,
                              const char *tag);

/*
 * Helper: store integer metadata
 */
int metadata_store_put_int64(metadata_store_t *store,
                             const char *key,
                             int64_t value,
                             const char *tag);

/*
 * Helper: store double metadata
 */
int metadata_store_put_double(metadata_store_t *store,
                              const char *key,
                              double value,
                              const char *tag);

/*
 * Helper: retrieve string metadata
 *
 * value: output buffer (caller allocated)
 * value_size: size of output buffer
 */
int metadata_store_get_string(metadata_store_t *store,
                              const char *key,
                              char *value,
                              size_t value_size);

/*
 * Helper: retrieve integer metadata
 */
int metadata_store_get_int64(metadata_store_t *store,
                             const char *key,
                             int64_t *value);

/*
 * Helper: retrieve double metadata
 */
int metadata_store_get_double(metadata_store_t *store,
                              const char *key,
                              double *value);

/*
 * Store task metadata for a completed task
 *
 * Stores comprehensive metadata about a task execution including
 * timing, resources, and status information.
 */
int metadata_store_task_info(metadata_store_t *store,
                             uint64_t task_id,
                             uint32_t model_id,
                             int status,
                             uint64_t exec_time_us,
                             uint64_t queue_time_us,
                             uint32_t worker_id);

/*
 * Store experiment configuration
 *
 * Stores experiment-level metadata like parameter sweeps,
 * model configuration, and run identifiers.
 */
int metadata_store_experiment(metadata_store_t *store,
                              const char *experiment_id,
                              const char *description,
                              const char *model_name,
                              const char *config_json);

#ifdef __cplusplus
}
#endif

#endif /* METADATA_H */