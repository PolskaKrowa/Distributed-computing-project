#ifndef STORAGE_METADATA_H
#define STORAGE_METADATA_H

#include <stddef.h>

/* Opaque metadata store type */
typedef struct metadata_store metadata_store_t;

/* Create and destroy */
metadata_store_t *metadata_create(void);
void metadata_destroy(metadata_store_t *store);

/* Set a key to a value. Overwrites existing value.
 * Returns 0 on success, non-zero on error (e.g. OOM). */
int metadata_set(metadata_store_t *store, const char *key, const char *value);

/* Get value for a key. Returns NULL if not found.
 * The returned pointer is owned by the store and remains valid until value is
 * changed or the store is destroyed. Do not free it. */
const char *metadata_get(const metadata_store_t *store, const char *key);

/* Remove a key. Returns 0 if removed, 1 if not found, negative on error. */
int metadata_remove(metadata_store_t *store, const char *key);

/* Number of entries in the store */
size_t metadata_count(const metadata_store_t *store);

/* Produce a JSON string of the form {"k":"v", ... }.
 * Returns a newly allocated string which the caller must free, or NULL on error. */
char *metadata_serialize_json(const metadata_store_t *store);

/* Load metadata from a JSON string produced by metadata_serialize_json.
 * Existing entries are cleared. Returns 0 on success, non-zero on parse/error. */
int metadata_load_json(metadata_store_t *store, const char *json);

/* Iterate entries. The callback is called for each key/value pair.
 * Iteration stops if callback returns non-zero; that value is returned.
 * Otherwise returns 0 on success. */
typedef int (*metadata_iter_cb)(const char *key, const char *value, void *userdata);
int metadata_iterate(const metadata_store_t *store, metadata_iter_cb cb, void *userdata);

#endif /* STORAGE_METADATA_H */
