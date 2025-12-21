#include "metadata.h"
#include "../common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

/* Internal store structure */
struct metadata_store {
    char path[4096];
    int read_only;
};

/* File format magic and version */
#define META_FILE_MAGIC 0x4D455441  /* "META" */
#define META_FILE_VERSION 1

/* Internal error codes */
#define META_OK 0
#define META_ERR_INVALID_ARG 1
#define META_ERR_NOT_FOUND 2
#define META_ERR_IO 3
#define META_ERR_EXISTS 4
#define META_ERR_CORRUPT 5

/* Helper: sanitise key for filename (replace / and .. with _) */
static void sanitise_key(const char *key, char *safe_key, size_t safe_size)
{
    size_t i, j = 0;
    for (i = 0; key[i] && j < safe_size - 1; ++i) {
        if (key[i] == '/' || key[i] == '\\') {
            safe_key[j++] = '_';
        } else if (key[i] == '.' && key[i+1] == '.') {
            safe_key[j++] = '_';
            safe_key[j++] = '_';
            ++i;
        } else {
            safe_key[j++] = key[i];
        }
    }
    safe_key[j] = '\0';
}

/* Helper: build full path for metadata file */
static void build_file_path(const metadata_store_t *store,
                           const char *key,
                           char *path,
                           size_t path_size)
{
    char safe_key[META_MAX_KEY_LEN];
    sanitise_key(key, safe_key, sizeof(safe_key));
    snprintf(path, path_size, "%s/%s.meta", store->path, safe_key);
}

/* Helper: write entry to file */
static int write_entry_to_file(const char *path, const metadata_entry_t *entry)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        log_error("Failed to open metadata file for writing: %s (%s)",
                  path, strerror(errno));
        return META_ERR_IO;
    }

    /* Write magic and version */
    uint32_t magic = META_FILE_MAGIC;
    uint32_t version = META_FILE_VERSION;
    if (fwrite(&magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1) {
        log_error("Failed to write metadata header to %s", path);
        fclose(f);
        return META_ERR_IO;
    }

    /* Write entry structure */
    if (fwrite(entry->key, sizeof(entry->key), 1, f) != 1 ||
        fwrite(&entry->type, sizeof(entry->type), 1, f) != 1 ||
        fwrite(&entry->tag_count, sizeof(entry->tag_count), 1, f) != 1 ||
        fwrite(&entry->created, sizeof(entry->created), 1, f) != 1 ||
        fwrite(&entry->modified, sizeof(entry->modified), 1, f) != 1) {
        log_error("Failed to write metadata fields to %s", path);
        fclose(f);
        return META_ERR_IO;
    }

    /* Write tags */
    if (fwrite(entry->tags, sizeof(entry->tags), 1, f) != 1) {
        log_error("Failed to write metadata tags to %s", path);
        fclose(f);
        return META_ERR_IO;
    }

    /* Write value based on type */
    switch (entry->type) {
        case META_TYPE_STRING:
            if (fwrite(entry->value.string, sizeof(entry->value.string), 1, f) != 1) {
                log_error("Failed to write string value to %s", path);
                fclose(f);
                return META_ERR_IO;
            }
            break;

        case META_TYPE_INT64:
            if (fwrite(&entry->value.i64, sizeof(entry->value.i64), 1, f) != 1) {
                log_error("Failed to write int64 value to %s", path);
                fclose(f);
                return META_ERR_IO;
            }
            break;

        case META_TYPE_DOUBLE:
            if (fwrite(&entry->value.f64, sizeof(entry->value.f64), 1, f) != 1) {
                log_error("Failed to write double value to %s", path);
                fclose(f);
                return META_ERR_IO;
            }
            break;

        case META_TYPE_TIMESTAMP:
            if (fwrite(&entry->value.timestamp, sizeof(entry->value.timestamp), 1, f) != 1) {
                log_error("Failed to write timestamp value to %s", path);
                fclose(f);
                return META_ERR_IO;
            }
            break;

        case META_TYPE_BLOB:
            /* Write blob size then data */
            if (fwrite(&entry->value.blob.size, sizeof(entry->value.blob.size), 1, f) != 1) {
                log_error("Failed to write blob size to %s", path);
                fclose(f);
                return META_ERR_IO;
            }
            if (entry->value.blob.size > 0 && entry->value.blob.data) {
                if (fwrite(entry->value.blob.data, entry->value.blob.size, 1, f) != 1) {
                    log_error("Failed to write blob data to %s", path);
                    fclose(f);
                    return META_ERR_IO;
                }
            }
            break;

        default:
            log_error("Unknown metadata type %d", entry->type);
            fclose(f);
            return META_ERR_INVALID_ARG;
    }

    fclose(f);
    return META_OK;
}

/* Helper: read entry from file */
static int read_entry_from_file(const char *path, metadata_entry_t *entry)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return META_ERR_NOT_FOUND;
    }

    /* Read and validate magic and version */
    uint32_t magic, version;
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1) {
        log_error("Failed to read metadata header from %s", path);
        fclose(f);
        return META_ERR_CORRUPT;
    }

    if (magic != META_FILE_MAGIC) {
        log_error("Invalid metadata magic in %s (got 0x%08x)", path, magic);
        fclose(f);
        return META_ERR_CORRUPT;
    }

    if (version != META_FILE_VERSION) {
        log_warn("Metadata version mismatch in %s (got %u, expected %u)",
                 path, version, META_FILE_VERSION);
    }

    /* Read entry structure */
    memset(entry, 0, sizeof(*entry));
    if (fread(entry->key, sizeof(entry->key), 1, f) != 1 ||
        fread(&entry->type, sizeof(entry->type), 1, f) != 1 ||
        fread(&entry->tag_count, sizeof(entry->tag_count), 1, f) != 1 ||
        fread(&entry->created, sizeof(entry->created), 1, f) != 1 ||
        fread(&entry->modified, sizeof(entry->modified), 1, f) != 1) {
        log_error("Failed to read metadata fields from %s", path);
        fclose(f);
        return META_ERR_CORRUPT;
    }

    /* Read tags */
    if (fread(entry->tags, sizeof(entry->tags), 1, f) != 1) {
        log_error("Failed to read metadata tags from %s", path);
        fclose(f);
        return META_ERR_CORRUPT;
    }

    /* Read value based on type */
    switch (entry->type) {
        case META_TYPE_STRING:
            if (fread(entry->value.string, sizeof(entry->value.string), 1, f) != 1) {
                log_error("Failed to read string value from %s", path);
                fclose(f);
                return META_ERR_CORRUPT;
            }
            break;

        case META_TYPE_INT64:
            if (fread(&entry->value.i64, sizeof(entry->value.i64), 1, f) != 1) {
                log_error("Failed to read int64 value from %s", path);
                fclose(f);
                return META_ERR_CORRUPT;
            }
            break;

        case META_TYPE_DOUBLE:
            if (fread(&entry->value.f64, sizeof(entry->value.f64), 1, f) != 1) {
                log_error("Failed to read double value from %s", path);
                fclose(f);
                return META_ERR_CORRUPT;
            }
            break;

        case META_TYPE_TIMESTAMP:
            if (fread(&entry->value.timestamp, sizeof(entry->value.timestamp), 1, f) != 1) {
                log_error("Failed to read timestamp value from %s", path);
                fclose(f);
                return META_ERR_CORRUPT;
            }
            break;

        case META_TYPE_BLOB:
            /* Read blob size */
            if (fread(&entry->value.blob.size, sizeof(entry->value.blob.size), 1, f) != 1) {
                log_error("Failed to read blob size from %s", path);
                fclose(f);
                return META_ERR_CORRUPT;
            }
            /* Allocate and read blob data */
            if (entry->value.blob.size > 0) {
                entry->value.blob.data = malloc(entry->value.blob.size);
                if (!entry->value.blob.data) {
                    log_error("Failed to allocate %zu bytes for blob data", entry->value.blob.size);
                    fclose(f);
                    return META_ERR_IO;
                }
                if (fread(entry->value.blob.data, entry->value.blob.size, 1, f) != 1) {
                    log_error("Failed to read blob data from %s", path);
                    free(entry->value.blob.data);
                    fclose(f);
                    return META_ERR_CORRUPT;
                }
            }
            break;

        default:
            log_error("Unknown metadata type %d in %s", entry->type, path);
            fclose(f);
            return META_ERR_CORRUPT;
    }

    fclose(f);
    return META_OK;
}

metadata_store_t *metadata_store_open(const char *path, int create)
{
    if (!path) {
        log_error("metadata_store_open: NULL path provided");
        return NULL;
    }

    /* Check if directory exists */
    struct stat st;
    if (stat(path, &st) != 0) {
        if (!create) {
            log_error("Metadata store path does not exist: %s", path);
            return NULL;
        }
        /* Create directory */
        if (mkdir(path, 0755) != 0) {
            log_error("Failed to create metadata store directory %s: %s",
                      path, strerror(errno));
            return NULL;
        }
        log_info("Created metadata store directory: %s", path);
    } else if (!S_ISDIR(st.st_mode)) {
        log_error("Metadata store path is not a directory: %s", path);
        return NULL;
    }

    /* Allocate store structure */
    metadata_store_t *store = calloc(1, sizeof(*store));
    if (!store) {
        log_error("Failed to allocate metadata store structure");
        return NULL;
    }

    strncpy(store->path, path, sizeof(store->path) - 1);
    store->read_only = 0;

    log_info("Opened metadata store: %s", path);
    return store;
}

void metadata_store_close(metadata_store_t *store)
{
    if (!store) return;

    log_info("Closed metadata store: %s", store->path);
    free(store);
}

int metadata_store_put(metadata_store_t *store, const metadata_entry_t *entry)
{
    if (!store || !entry) {
        log_error("metadata_store_put: NULL argument");
        return META_ERR_INVALID_ARG;
    }

    if (store->read_only) {
        log_error("Metadata store is read-only");
        return META_ERR_INVALID_ARG;
    }

    if (strlen(entry->key) == 0 || strlen(entry->key) >= META_MAX_KEY_LEN) {
        log_error("Invalid metadata key length: %zu", strlen(entry->key));
        return META_ERR_INVALID_ARG;
    }

    char path[4096];
    build_file_path(store, entry->key, path, sizeof(path));

    int rc = write_entry_to_file(path, entry);
    if (rc != META_OK) {
        log_error("Failed to write metadata entry '%s'", entry->key);
        return rc;
    }

    log_debug("Stored metadata: key='%s' type=%d", entry->key, entry->type);
    return META_OK;
}

int metadata_store_get(metadata_store_t *store, const char *key, metadata_entry_t *entry)
{
    if (!store || !key || !entry) {
        log_error("metadata_store_get: NULL argument");
        return META_ERR_INVALID_ARG;
    }

    char path[4096];
    build_file_path(store, key, path, sizeof(path));

    int rc = read_entry_from_file(path, entry);
    if (rc == META_ERR_NOT_FOUND) {
        log_debug("Metadata not found: key='%s'", key);
        return rc;
    }
    if (rc != META_OK) {
        log_error("Failed to read metadata entry '%s'", key);
        return rc;
    }

    log_debug("Retrieved metadata: key='%s' type=%d", key, entry->type);
    return META_OK;
}

int metadata_store_delete(metadata_store_t *store, const char *key)
{
    if (!store || !key) {
        log_error("metadata_store_delete: NULL argument");
        return META_ERR_INVALID_ARG;
    }

    if (store->read_only) {
        log_error("Metadata store is read-only");
        return META_ERR_INVALID_ARG;
    }

    char path[4096];
    build_file_path(store, key, path, sizeof(path));

    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            return META_ERR_NOT_FOUND;
        }
        log_error("Failed to delete metadata file %s: %s", path, strerror(errno));
        return META_ERR_IO;
    }

    log_debug("Deleted metadata: key='%s'", key);
    return META_OK;
}

int metadata_store_query(metadata_store_t *store,
                         const metadata_query_t *query,
                         metadata_entry_t *results,
                         size_t max_results,
                         size_t *count)
{
    if (!store || !results || !count) {
        log_error("metadata_store_query: NULL argument provided in function call");
        return META_ERR_INVALID_ARG;
    }

    *count = 0;
    DIR *dir = opendir(store->path);
    if (!dir) {
        log_error("Failed to open metadata directory %s: %s",
                  store->path, strerror(errno));
        return META_ERR_IO;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && *count < max_results) {
        /* Skip . and .. */
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        /* Skip non-.meta files */
        size_t len = strlen(ent->d_name);
        if (len < 6 || strcmp(ent->d_name + len - 5, ".meta") != 0) {
            continue;
        }

        /* Read entry */
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", store->path, ent->d_name);
        
        metadata_entry_t entry;
        if (read_entry_from_file(path, &entry) != META_OK) {
            continue;
        }

        /* Apply filters */
        int match = 1;
        
        if (query) {
            /* Key prefix filter */
            if (query->key_prefix && strncmp(entry.key, query->key_prefix, strlen(query->key_prefix)) != 0) {
                match = 0;
            }

            /* Type filter */
            if (match && query->type >= 0 && entry.type != query->type) {
                match = 0;
            }

            /* Tag filter */
            if (match && query->tag) {
                int tag_found = 0;
                for (int i = 0; i < entry.tag_count; ++i) {
                    if (strcmp(entry.tags[i], query->tag) == 0) {
                        tag_found = 1;
                        break;
                    }
                }
                if (!tag_found) match = 0;
            }

            /* Time filters */
            if (match && query->created_after > 0 && entry.created < query->created_after) {
                match = 0;
            }
            if (match && query->created_before > 0 && entry.created > query->created_before) {
                match = 0;
            }
        }

        if (match) {
            results[*count] = entry;
            (*count)++;
        } else {
            /* Free blob data if not included in results */
            if (entry.type == META_TYPE_BLOB && entry.value.blob.data) {
                free(entry.value.blob.data);
            }
        }
    }

    closedir(dir);
    log_debug("Metadata query returned %zu results", *count);
    return META_OK;
}

/* Helper functions */

int metadata_store_put_string(metadata_store_t *store,
                              const char *key,
                              const char *value,
                              const char *tag)
{
    metadata_entry_t entry = {0};
    strncpy(entry.key, key, sizeof(entry.key) - 1);
    entry.type = META_TYPE_STRING;
    strncpy(entry.value.string, value, sizeof(entry.value.string) - 1);
    entry.created = time(NULL);
    entry.modified = entry.created;
    
    if (tag) {
        strncpy(entry.tags[0], tag, sizeof(entry.tags[0]) - 1);
        entry.tag_count = 1;
    }

    return metadata_store_put(store, &entry);
}

int metadata_store_put_int64(metadata_store_t *store,
                             const char *key,
                             int64_t value,
                             const char *tag)
{
    metadata_entry_t entry = {0};
    strncpy(entry.key, key, sizeof(entry.key) - 1);
    entry.type = META_TYPE_INT64;
    entry.value.i64 = value;
    entry.created = time(NULL);
    entry.modified = entry.created;
    
    if (tag) {
        strncpy(entry.tags[0], tag, sizeof(entry.tags[0]) - 1);
        entry.tag_count = 1;
    }

    return metadata_store_put(store, &entry);
}

int metadata_store_put_double(metadata_store_t *store,
                              const char *key,
                              double value,
                              const char *tag)
{
    metadata_entry_t entry = {0};
    strncpy(entry.key, key, sizeof(entry.key) - 1);
    entry.type = META_TYPE_DOUBLE;
    entry.value.f64 = value;
    entry.created = time(NULL);
    entry.modified = entry.created;
    
    if (tag) {
        strncpy(entry.tags[0], tag, sizeof(entry.tags[0]) - 1);
        entry.tag_count = 1;
    }

    return metadata_store_put(store, &entry);
}

int metadata_store_get_string(metadata_store_t *store,
                              const char *key,
                              char *value,
                              size_t value_size)
{
    metadata_entry_t entry;
    int rc = metadata_store_get(store, key, &entry);
    if (rc != META_OK) return rc;

    if (entry.type != META_TYPE_STRING) {
        log_error("Metadata type mismatch for key '%s': expected STRING, got %d", key, entry.type);
        return META_ERR_INVALID_ARG;
    }

    strncpy(value, entry.value.string, value_size - 1);
    value[value_size - 1] = '\0';
    return META_OK;
}

int metadata_store_get_int64(metadata_store_t *store,
                             const char *key,
                             int64_t *value)
{
    metadata_entry_t entry;
    int rc = metadata_store_get(store, key, &entry);
    if (rc != META_OK) return rc;

    if (entry.type != META_TYPE_INT64) {
        log_error("Metadata type mismatch for key '%s': expected INT64, got %d", key, entry.type);
        return META_ERR_INVALID_ARG;
    }

    *value = entry.value.i64;
    return META_OK;
}

int metadata_store_get_double(metadata_store_t *store,
                              const char *key,
                              double *value)
{
    metadata_entry_t entry;
    int rc = metadata_store_get(store, key, &entry);
    if (rc != META_OK) return rc;

    if (entry.type != META_TYPE_DOUBLE) {
        log_error("Metadata type mismatch for key '%s': expected DOUBLE, got %d", key, entry.type);
        return META_ERR_INVALID_ARG;
    }

    *value = entry.value.f64;
    return META_OK;
}

int metadata_store_task_info(metadata_store_t *store,
                             uint64_t task_id,
                             uint32_t model_id,
                             int status,
                             uint64_t exec_time_us,
                             uint64_t queue_time_us,
                             uint32_t worker_id)
{
    char key[256];
    snprintf(key, sizeof(key), "task.%lu.model_id", task_id);
    metadata_store_put_int64(store, key, model_id, "task");

    snprintf(key, sizeof(key), "task.%lu.status", task_id);
    metadata_store_put_int64(store, key, status, "task");

    snprintf(key, sizeof(key), "task.%lu.exec_time_us", task_id);
    metadata_store_put_int64(store, key, exec_time_us, "task");

    snprintf(key, sizeof(key), "task.%lu.queue_time_us", task_id);
    metadata_store_put_int64(store, key, queue_time_us, "task");

    snprintf(key, sizeof(key), "task.%lu.worker_id", task_id);
    metadata_store_put_int64(store, key, worker_id, "task");

    log_debug("Stored task metadata: task_id=%lu model=%u status=%d", task_id, model_id, status);
    return META_OK;
}

int metadata_store_experiment(metadata_store_t *store,
                              const char *experiment_id,
                              const char *description,
                              const char *model_name,
                              const char *config_json)
{
    char key[256];
    
    snprintf(key, sizeof(key), "experiment.%s.description", experiment_id);
    metadata_store_put_string(store, key, description, "experiment");

    snprintf(key, sizeof(key), "experiment.%s.model", experiment_id);
    metadata_store_put_string(store, key, model_name, "experiment");

    if (config_json) {
        snprintf(key, sizeof(key), "experiment.%s.config", experiment_id);
        metadata_store_put_string(store, key, config_json, "experiment");
    }

    log_info("Stored experiment metadata: id=%s model=%s", experiment_id, model_name);
    return META_OK;
}