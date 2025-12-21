#include "hdf5_index.h"
#include "../common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <hdf5.h>

/* Internal error codes */
#define HDF5_OK 0
#define HDF5_ERR_INVALID_ARG 1
#define HDF5_ERR_NOT_FOUND 2
#define HDF5_ERR_IO 3
#define HDF5_ERR_HDF5 4
#define HDF5_ERR_DB 5

/* Index structure */
struct hdf5_index {
    char index_path[HDF5_MAX_PATH_LEN];
    sqlite3 *db;
};

/* Helper: create SQLite schema */
static int create_schema(sqlite3 *db)
{
    const char *schema = 
        "CREATE TABLE IF NOT EXISTS files ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  filepath TEXT UNIQUE NOT NULL,"
        "  task_id INTEGER NOT NULL,"
        "  model_id INTEGER NOT NULL,"
        "  created INTEGER NOT NULL,"
        "  file_size INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_task_id ON files(task_id);"
        "CREATE INDEX IF NOT EXISTS idx_model_id ON files(model_id);"
        "CREATE INDEX IF NOT EXISTS idx_created ON files(created);"
        ""
        "CREATE TABLE IF NOT EXISTS datasets ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  file_id INTEGER NOT NULL,"
        "  name TEXT NOT NULL,"
        "  type INTEGER NOT NULL,"
        "  rank INTEGER NOT NULL,"
        "  element_size INTEGER NOT NULL,"
        "  dtype_name TEXT,"
        "  FOREIGN KEY(file_id) REFERENCES files(id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_dataset_name ON datasets(name);";

    char *errmsg = NULL;
    int rc = sqlite3_exec(db, schema, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        log_error("Failed to create schema: %s", errmsg ? errmsg : "unknown error");
        sqlite3_free(errmsg);
        return HDF5_ERR_DB;
    }

    return HDF5_OK;
}

hdf5_index_t *hdf5_index_open(const char *index_path, int create)
{
    if (!index_path) {
        log_error("hdf5_index_open: NULL index_path");
        return NULL;
    }

    /* Check if file exists */
    struct stat st;
    int exists = (stat(index_path, &st) == 0);

    if (!exists && !create) {
        log_error("Index file does not exist and create=0: %s", index_path);
        return NULL;
    }

    /* Allocate index structure */
    hdf5_index_t *idx = calloc(1, sizeof(*idx));
    if (!idx) {
        log_error("Failed to allocate hdf5_index structure");
        return NULL;
    }

    strncpy(idx->index_path, index_path, sizeof(idx->index_path) - 1);

    /* Open SQLite database */
    int rc = sqlite3_open(index_path, &idx->db);
    if (rc != SQLITE_OK) {
        log_error("Failed to open index database %s: %s",
                  index_path, sqlite3_errmsg(idx->db));
        sqlite3_close(idx->db);
        free(idx);
        return NULL;
    }

    /* Enable foreign keys */
    sqlite3_exec(idx->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    /* Create schema if new database */
    if (create_schema(idx->db) != HDF5_OK) {
        sqlite3_close(idx->db);
        free(idx);
        return NULL;
    }

    log_info("Opened HDF5 index: %s", index_path);
    return idx;
}

void hdf5_index_close(hdf5_index_t *idx)
{
    if (!idx) return;

    if (idx->db) {
        sqlite3_close(idx->db);
    }

    log_info("Closed HDF5 index: %s", idx->index_path);
    free(idx);
}

/* Helper: scan HDF5 file and extract dataset info */
static int scan_hdf5_file(const char *filepath, hdf5_file_entry_t *entry)
{
    hid_t file_id = H5Fopen(filepath, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file_id < 0) {
        log_error("Failed to open HDF5 file: %s", filepath);
        return HDF5_ERR_HDF5;
    }

    /* Get file size */
    struct stat st;
    if (stat(filepath, &st) == 0) {
        entry->file_size = st.st_size;
        entry->created = st.st_mtime;
    }

    /* List datasets using H5Literate (simplified version) */
    entry->dataset_count = 0;

    /* For now, we'll just check for common dataset names */
    const char *common_datasets[] = {"input", "output", "state", "checkpoint", "metadata"};
    hdf5_dataset_type_t dataset_types[] = {
        HDF5_DATASET_INPUT,
        HDF5_DATASET_OUTPUT,
        HDF5_DATASET_STATE,
        HDF5_DATASET_CHECKPOINT,
        HDF5_DATASET_METADATA
    };

    for (int i = 0; i < 5 && entry->dataset_count < 32; ++i) {
        /* Suppress HDF5 error messages for datasets that don't exist */
        H5E_auto_t old_func;
        void *old_client_data;
        H5Eget_auto(H5E_DEFAULT, &old_func, &old_client_data);
        H5Eset_auto(H5E_DEFAULT, NULL, NULL);
        
        hid_t dataset = H5Dopen2(file_id, common_datasets[i], H5P_DEFAULT);
        
        /* Restore error handler */
        H5Eset_auto(H5E_DEFAULT, old_func, old_client_data);
        
        if (dataset >= 0) {
            hdf5_dataset_info_t *ds_info = &entry->datasets[entry->dataset_count];
            strncpy(ds_info->name, common_datasets[i], sizeof(ds_info->name) - 1);
            ds_info->type = dataset_types[i];

            /* Get dataspace info */
            hid_t space = H5Dget_space(dataset);
            if (space >= 0) {
                ds_info->rank = H5Sget_simple_extent_ndims(space);
                if (ds_info->rank <= 8) {
                    hsize_t dims[8];
                    H5Sget_simple_extent_dims(space, dims, NULL);
                    for (size_t j = 0; j < ds_info->rank; ++j) {
                        ds_info->dims[j] = dims[j];
                    }
                }
                H5Sclose(space);
            }

            /* Get datatype info */
            hid_t dtype = H5Dget_type(dataset);
            if (dtype >= 0) {
                ds_info->element_size = H5Tget_size(dtype);
                
                /* Determine type name */
                H5T_class_t type_class = H5Tget_class(dtype);
                switch (type_class) {
                    case H5T_INTEGER:
                        strncpy(ds_info->dtype_name, "integer", sizeof(ds_info->dtype_name) - 1);
                        break;
                    case H5T_FLOAT:
                        strncpy(ds_info->dtype_name, "float", sizeof(ds_info->dtype_name) - 1);
                        break;
                    case H5T_STRING:
                        strncpy(ds_info->dtype_name, "string", sizeof(ds_info->dtype_name) - 1);
                        break;
                    default:
                        strncpy(ds_info->dtype_name, "unknown", sizeof(ds_info->dtype_name) - 1);
                }
                
                H5Tclose(dtype);
            }

            H5Dclose(dataset);
            entry->dataset_count++;
        }
    }

    H5Fclose(file_id);
    return HDF5_OK;
}

int hdf5_index_add_file(hdf5_index_t *idx,
                        const char *filepath,
                        uint64_t task_id,
                        uint32_t model_id)
{
    if (!idx || !filepath) {
        log_error("hdf5_index_add_file: NULL argument");
        return HDF5_ERR_INVALID_ARG;
    }

    /* Scan HDF5 file */
    hdf5_file_entry_t entry = {0};
    strncpy(entry.filepath, filepath, sizeof(entry.filepath) - 1);
    entry.task_id = task_id;
    entry.model_id = model_id;

    int rc = scan_hdf5_file(filepath, &entry);
    if (rc != HDF5_OK) {
        log_error("Failed to scan HDF5 file: %s", filepath);
        return rc;
    }

    /* Insert into database */
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO files "
                     "(filepath, task_id, model_id, created, file_size) "
                     "VALUES (?, ?, ?, ?, ?)";

    rc = sqlite3_prepare_v2(idx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare insert statement: %s", sqlite3_errmsg(idx->db));
        return HDF5_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, entry.filepath, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, entry.task_id);
    sqlite3_bind_int(stmt, 3, entry.model_id);
    sqlite3_bind_int64(stmt, 4, entry.created);
    sqlite3_bind_int64(stmt, 5, entry.file_size);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log_error("Failed to insert file entry: %s", sqlite3_errmsg(idx->db));
        return HDF5_ERR_DB;
    }

    int64_t file_id = sqlite3_last_insert_rowid(idx->db);

    /* Insert dataset info */
    sql = "INSERT INTO datasets (file_id, name, type, rank, element_size, dtype_name) "
          "VALUES (?, ?, ?, ?, ?, ?)";

    for (int i = 0; i < entry.dataset_count; ++i) {
        rc = sqlite3_prepare_v2(idx->db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) continue;

        sqlite3_bind_int64(stmt, 1, file_id);
        sqlite3_bind_text(stmt, 2, entry.datasets[i].name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, entry.datasets[i].type);
        sqlite3_bind_int(stmt, 4, entry.datasets[i].rank);
        sqlite3_bind_int64(stmt, 5, entry.datasets[i].element_size);
        sqlite3_bind_text(stmt, 6, entry.datasets[i].dtype_name, -1, SQLITE_STATIC);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    log_debug("Added HDF5 file to index: task_id=%lu filepath=%s datasets=%d",
              task_id, filepath, entry.dataset_count);
    return HDF5_OK;
}

int hdf5_index_remove_file(hdf5_index_t *idx, const char *filepath)
{
    if (!idx || !filepath) {
        log_error("hdf5_index_remove_file: NULL argument");
        return HDF5_ERR_INVALID_ARG;
    }

    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM files WHERE filepath = ?";

    int rc = sqlite3_prepare_v2(idx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare delete statement: %s", sqlite3_errmsg(idx->db));
        return HDF5_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log_error("Failed to delete file entry: %s", sqlite3_errmsg(idx->db));
        return HDF5_ERR_DB;
    }

    log_debug("Removed file from index: %s", filepath);
    return HDF5_OK;
}

int hdf5_index_query(hdf5_index_t *idx,
                     const hdf5_query_t *query,
                     hdf5_file_entry_t *results,
                     size_t max_results,
                     size_t *count)
{
    if (!idx || !results || !count) {
        log_error("hdf5_index_query: NULL argument");
        return HDF5_ERR_INVALID_ARG;
    }

    *count = 0;

    /* Build query */
    char sql[1024];
    strcpy(sql, "SELECT filepath, task_id, model_id, created, file_size FROM files WHERE 1=1");

    if (query) {
        char buf[256];
        if (query->task_id > 0) {
            snprintf(buf, sizeof(buf), " AND task_id = %lu", query->task_id);
            strcat(sql, buf);
        }
        if (query->model_id > 0) {
            snprintf(buf, sizeof(buf), " AND model_id = %u", query->model_id);
            strcat(sql, buf);
        }
        if (query->created_after > 0) {
            snprintf(buf, sizeof(buf), " AND created >= %ld", query->created_after);
            strcat(sql, buf);
        }
        if (query->created_before > 0) {
            snprintf(buf, sizeof(buf), " AND created <= %ld", query->created_before);
            strcat(sql, buf);
        }
    }

    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " LIMIT %zu", max_results);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(idx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare query: %s", sqlite3_errmsg(idx->db));
        return HDF5_ERR_DB;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && *count < max_results) {
        hdf5_file_entry_t *entry = &results[*count];
        memset(entry, 0, sizeof(*entry));

        const char *filepath = (const char*)sqlite3_column_text(stmt, 0);
        strncpy(entry->filepath, filepath, sizeof(entry->filepath) - 1);
        entry->task_id = sqlite3_column_int64(stmt, 1);
        entry->model_id = sqlite3_column_int(stmt, 2);
        entry->created = sqlite3_column_int64(stmt, 3);
        entry->file_size = sqlite3_column_int64(stmt, 4);

        (*count)++;
    }

    sqlite3_finalize(stmt);

    log_debug("Query returned %zu results", *count);
    return HDF5_OK;
}

int hdf5_index_get_by_task(hdf5_index_t *idx,
                           uint64_t task_id,
                           hdf5_file_entry_t *entry)
{
    hdf5_query_t query = {0};
    query.task_id = task_id;

    size_t count;
    int rc = hdf5_index_query(idx, &query, entry, 1, &count);
    if (rc != HDF5_OK) return rc;

    if (count == 0) {
        return HDF5_ERR_NOT_FOUND;
    }

    return HDF5_OK;
}

int hdf5_create_task_file(hdf5_index_t *idx,
                          const char *output_dir,
                          uint64_t task_id,
                          uint32_t model_id,
                          char *filepath_out,
                          size_t filepath_size)
{
    if (!output_dir || !filepath_out) {
        log_error("hdf5_create_task_file: NULL argument");
        return HDF5_ERR_INVALID_ARG;
    }

    /* Create output directory if needed */
    struct stat st;
    if (stat(output_dir, &st) != 0) {
        if (mkdir(output_dir, 0755) != 0) {
            log_error("Failed to create output directory %s: %s",
                      output_dir, strerror(errno));
            return HDF5_ERR_IO;
        }
    }

    /* Build filepath */
    snprintf(filepath_out, filepath_size, "%s/task_%lu_model_%u.h5",
             output_dir, task_id, model_id);

    /* Create HDF5 file */
    hid_t file_id = H5Fcreate(filepath_out, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file_id < 0) {
        log_error("Failed to create HDF5 file: %s", filepath_out);
        return HDF5_ERR_HDF5;
    }

    /* Create groups for organisation */
    hid_t input_group = H5Gcreate2(file_id, "/input", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    hid_t output_group = H5Gcreate2(file_id, "/output", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    
    if (input_group >= 0) H5Gclose(input_group);
    if (output_group >= 0) H5Gclose(output_group);

    H5Fclose(file_id);

    /* Add to index */
    if (idx) {
        hdf5_index_add_file(idx, filepath_out, task_id, model_id);
    }

    log_info("Created HDF5 task file: %s", filepath_out);
    return HDF5_OK;
}

int hdf5_write_dataset(const char *filepath,
                       const char *dataset_name,
                       const void *data,
                       size_t rank,
                       const size_t *dims,
                       size_t element_size)
{
    if (!filepath || !dataset_name || !data || rank == 0 || !dims) {
        log_error("hdf5_write_dataset: invalid argument");
        return HDF5_ERR_INVALID_ARG;
    }

    hid_t file_id = H5Fopen(filepath, H5F_ACC_RDWR, H5P_DEFAULT);
    if (file_id < 0) {
        log_error("Failed to open HDF5 file for writing: %s", filepath);
        return HDF5_ERR_HDF5;
    }

    /* Create parent groups if needed (e.g., /input, /output) */
    char *dataset_copy = strdup(dataset_name);
    if (!dataset_copy) {
        H5Fclose(file_id);
        return HDF5_ERR_INVALID_ARG;
    }

    char *last_slash = strrchr(dataset_copy, '/');
    if (last_slash && last_slash != dataset_copy) {
        *last_slash = '\0';
        htri_t exists = H5Lexists(file_id, dataset_copy, H5P_DEFAULT);
        if (exists == 0) {
            /* Group doesn't exist, create it */
            hid_t group_id = H5Gcreate2(file_id, dataset_copy, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            if (group_id >= 0) {
                H5Gclose(group_id);
            }
        }
    }
    free(dataset_copy);

    /* Create dataspace */
    hsize_t h5_dims[8];
    for (size_t i = 0; i < rank && i < 8; ++i) {
        h5_dims[i] = dims[i];
    }

    hid_t space_id = H5Screate_simple(rank, h5_dims, NULL);
    if (space_id < 0) {
        log_error("Failed to create dataspace for %s", dataset_name);
        H5Fclose(file_id);
        return HDF5_ERR_HDF5;
    }

    /* Determine HDF5 type based on element size */
    hid_t dtype;
    if (element_size == sizeof(double)) {
        dtype = H5T_NATIVE_DOUBLE;
    } else if (element_size == sizeof(float)) {
        dtype = H5T_NATIVE_FLOAT;
    } else if (element_size == sizeof(int64_t)) {
        dtype = H5T_NATIVE_INT64;
    } else if (element_size == sizeof(int32_t)) {
        dtype = H5T_NATIVE_INT32;
    } else {
        dtype = H5T_NATIVE_CHAR;
    }

    /* Create dataset */
    hid_t dataset_id = H5Dcreate2(file_id, dataset_name, dtype, space_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dataset_id < 0) {
        log_error("Failed to create dataset %s", dataset_name);
        H5Sclose(space_id);
        H5Fclose(file_id);
        return HDF5_ERR_HDF5;
    }

    /* Write data */
    herr_t status = H5Dwrite(dataset_id, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
    if (status < 0) {
        log_error("Failed to write dataset %s", dataset_name);
        H5Dclose(dataset_id);
        H5Sclose(space_id);
        H5Fclose(file_id);
        return HDF5_ERR_HDF5;
    }

    H5Dclose(dataset_id);
    H5Sclose(space_id);
    H5Fclose(file_id);

    log_debug("Wrote dataset %s to %s", dataset_name, filepath);
    return HDF5_OK;
}

int hdf5_read_dataset(const char *filepath,
                      const char *dataset_name,
                      void *data_out,
                      size_t max_elements,
                      size_t *elements_read)
{
    if (!filepath || !dataset_name || !data_out || !elements_read) {
        log_error("hdf5_read_dataset: invalid argument");
        return HDF5_ERR_INVALID_ARG;
    }

    hid_t file_id = H5Fopen(filepath, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file_id < 0) {
        log_error("Failed to open HDF5 file: %s", filepath);
        return HDF5_ERR_HDF5;
    }

    /* Suppress error messages for datasets that might not exist */
    H5E_auto_t old_func;
    void *old_client_data;
    H5Eget_auto(H5E_DEFAULT, &old_func, &old_client_data);
    H5Eset_auto(H5E_DEFAULT, NULL, NULL);
    
    hid_t dataset_id = H5Dopen2(file_id, dataset_name, H5P_DEFAULT);
    
    /* Restore error handler */
    H5Eset_auto(H5E_DEFAULT, old_func, old_client_data);
    
    if (dataset_id < 0) {
        log_debug("Dataset %s not found in %s", dataset_name, filepath);
        H5Fclose(file_id);
        return HDF5_ERR_NOT_FOUND;
    }

    hid_t dtype = H5Dget_type(dataset_id);
    hid_t space_id = H5Dget_space(dataset_id);

    /* Get number of elements */
    hssize_t npoints = H5Sget_simple_extent_npoints(space_id);
    *elements_read = (npoints > (hssize_t)max_elements) ? max_elements : npoints;

    /* Read data */
    herr_t status = H5Dread(dataset_id, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, data_out);
    if (status < 0) {
        log_error("Failed to read dataset %s", dataset_name);
        H5Tclose(dtype);
        H5Sclose(space_id);
        H5Dclose(dataset_id);
        H5Fclose(file_id);
        return HDF5_ERR_HDF5;
    }

    H5Tclose(dtype);
    H5Sclose(space_id);
    H5Dclose(dataset_id);
    H5Fclose(file_id);

    log_debug("Read %zu elements from dataset %s", *elements_read, dataset_name);
    return HDF5_OK;
}

int hdf5_store_task_result(hdf5_index_t *idx,
                           const char *output_dir,
                           uint64_t task_id,
                           uint32_t model_id,
                           const void *input_data,
                           size_t input_size,
                           const void *output_data,
                           size_t output_size)
{
    char filepath[HDF5_MAX_PATH_LEN];
    int rc = hdf5_create_task_file(idx, output_dir, task_id, model_id,
                                    filepath, sizeof(filepath));
    if (rc != HDF5_OK) {
        return rc;
    }

    /* Write input data if provided */
    if (input_data && input_size > 0) {
        size_t dims[1] = {input_size / sizeof(double)};
        hdf5_write_dataset(filepath, "/input/data", input_data, 1, dims, sizeof(double));
    }

    /* Write output data */
    if (output_data && output_size > 0) {
        size_t dims[1] = {output_size / sizeof(double)};
        hdf5_write_dataset(filepath, "/output/data", output_data, 1, dims, sizeof(double));
    }

    log_info("Stored task result: task_id=%lu filepath=%s", task_id, filepath);
    return HDF5_OK;
}

int hdf5_index_get_stats(hdf5_index_t *idx, hdf5_index_stats_t *stats)
{
    if (!idx || !stats) {
        return HDF5_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));

    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*), SUM(file_size), COUNT(DISTINCT model_id), "
                     "MIN(created), MAX(created) FROM files";

    int rc = sqlite3_prepare_v2(idx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return HDF5_ERR_DB;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        stats->total_files = sqlite3_column_int64(stmt, 0);
        stats->total_size_bytes = sqlite3_column_int64(stmt, 1);
        stats->unique_models = sqlite3_column_int(stmt, 2);
        stats->oldest_file = sqlite3_column_int64(stmt, 3);
        stats->newest_file = sqlite3_column_int64(stmt, 4);
    }

    sqlite3_finalize(stmt);
    return HDF5_OK;
}

int hdf5_index_rebuild(hdf5_index_t *idx, const char *directory)
{
    if (!idx || !directory) {
        return HDF5_ERR_INVALID_ARG;
    }

    log_info("Rebuilding HDF5 index from directory: %s", directory);

    /* Clear existing entries */
    sqlite3_exec(idx->db, "DELETE FROM files", NULL, NULL, NULL);

    /* Scan directory for .h5 files */
    /* This is a simplified version - real implementation would use directory traversal */
    log_warn("hdf5_index_rebuild: not fully implemented");
    
    return HDF5_OK;
}