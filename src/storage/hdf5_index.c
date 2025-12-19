#define _POSIX_C_SOURCE 200809L
#include "hdf5_index.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#ifndef HAVE_HDF5

/* Fallback implementation: write/read a JSON sidecar file named:
 *   <target_filepath>.index.json
 */

static char *sidecar_path(const char *target) {
    size_t n = strlen(target) + 12 + 1; /* ".index.json" + NUL */
    char *p = malloc(n);
    if (!p) return NULL;
    snprintf(p, n, "%s.index.json", target);
    return p;
}

int hdf5_index_write(const char *target_filepath, const metadata_store_t *store) {
    if (!target_filepath || !store) return -1;
    char *path = sidecar_path(target_filepath);
    if (!path) return -1;
    char *json = metadata_serialize_json(store);
    if (!json) { free(path); return -1; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(path); free(json); return -1; }
    size_t w = fwrite(json, 1, strlen(json), f);
    fclose(f);
    free(json);
    free(path);
    return (w > 0) ? 0 : -1;
}

int hdf5_index_read(const char *target_filepath, metadata_store_t *store) {
    if (!target_filepath || !store) return -1;
    char *path = sidecar_path(target_filepath);
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) { free(path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); free(path); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); free(path); return -1; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); free(path); return -1; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[r] = '\0';
    int rc = metadata_load_json(store, buf);
    free(buf);
    free(path);
    return rc;
}

int hdf5_index_exists(const char *target_filepath) {
    if (!target_filepath) return -1;
    char *path = sidecar_path(target_filepath);
    if (!path) return -1;
    struct stat st;
    int rc = stat(path, &st);
    free(path);
    if (rc == 0) return 1;
    if (rc == -1) return 0;
    return 0;
}

#else /* HAVE_HDF5 */

/* Minimal HDF5 implementation that stores two variable-length string datasets:
 *  - /index/keys
 *  - /index/values
 *
 * This keeps the on-disk index HDF5-native and easy to read.
 *
 * Build with -DHAVE_HDF5 and link to -lhdf5.
 */

#include <hdf5.h>

static int write_hdf5_strings(hid_t file_id, const char *group, const char *dset_name,
                              const char **strings, size_t n) {
    hid_t gid = H5Gcreate2(file_id, group, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (gid < 0) return -1;

    hid_t strtype = H5Tcopy(H5T_C_S1);
    H5Tset_size(strtype, H5T_VARIABLE);

    hsize_t dims[1] = { n };
    hid_t space = H5Screate_simple(1, dims, NULL);
    if (space < 0) { H5Gclose(gid); H5Tclose(strtype); return -1; }

    hid_t dset = H5Dcreate2(gid, dset_name, strtype, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dset < 0) { H5Sclose(space); H5Gclose(gid); H5Tclose(strtype); return -1; }

    herr_t herr = H5Dwrite(dset, strtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, strings);
    H5Dclose(dset);
    H5Sclose(space);
    H5Tclose(strtype);
    H5Gclose(gid);
    return (herr < 0) ? -1 : 0;
}

int hdf5_index_write(const char *target_filepath, const metadata_store_t *store) {
    if (!target_filepath || !store) return -1;
    /* Count entries and collect keys/values */
    size_t n = metadata_count(store);
    const char **keys = calloc(n, sizeof(char*));
    const char **vals = calloc(n, sizeof(char*));
    if (!keys || !vals) { free(keys); free(vals); return -1; }
    size_t i = 0;
    int cb(const char *k, const char *v, void *ud) {
        size_t *p = ud;
        keys[*p] = k;
        vals[*p] = v ? v : "";
        (*p)++;
        return 0;
    }
    size_t idx = 0;
    metadata_iterate(store, cb, &idx);

    /* Create file */
    hid_t file = H5Fcreate(target_filepath, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0) { free(keys); free(vals); return -1; }

    int rc1 = write_hdf5_strings(file, "/index", "keys", keys, n);
    int rc2 = write_hdf5_strings(file, "/index", "values", vals, n);

    H5Fflush(file, H5F_SCOPE_GLOBAL);
    H5Fclose(file);
    free(keys);
    free(vals);
    return (rc1 == 0 && rc2 == 0) ? 0 : -1;
}

int hdf5_index_read(const char *target_filepath, metadata_store_t *store) {
    if (!target_filepath || !store) return -1;
    hid_t file = H5Fopen(target_filepath, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return -1;

    /* Open group and datasets */
    hid_t gid = H5Gopen2(file, "/index", H5P_DEFAULT);
    if (gid < 0) { H5Fclose(file); return -1; }

    hid_t dset_k = H5Dopen2(gid, "keys", H5P_DEFAULT);
    hid_t dset_v = H5Dopen2(gid, "values", H5P_DEFAULT);
    if (dset_k < 0 || dset_v < 0) {
        if (dset_k >= 0) H5Dclose(dset_k);
        if (dset_v >= 0) H5Dclose(dset_v);
        H5Gclose(gid); H5Fclose(file);
        return -1;
    }

    hid_t stype = H5Dget_type(dset_k);
    hid_t space = H5Dget_space(dset_k);
    hsize_t dims[1];
    int nd = H5Sget_simple_extent_dims(space, dims, NULL);
    size_t n = (size_t)dims[0];

    /* Read variable-length string arrays */
    char **keys = calloc(n, sizeof(char*));
    char **vals = calloc(n, sizeof(char*));
    if (!keys || !vals) {
        free(keys); free(vals);
        H5Tclose(stype); H5Sclose(space);
        H5Dclose(dset_k); H5Dclose(dset_v);
        H5Gclose(gid); H5Fclose(file);
        return -1;
    }

    if (H5Dread(dset_k, stype, H5S_ALL, H5S_ALL, H5P_DEFAULT, keys) < 0) {
        free(keys); free(vals);
        H5Tclose(stype); H5Sclose(space);
        H5Dclose(dset_k); H5Dclose(dset_v);
        H5Gclose(gid); H5Fclose(file);
        return -1;
    }
    if (H5Dread(dset_v, stype, H5S_ALL, H5S_ALL, H5P_DEFAULT, vals) < 0) {
        /* free HDF5-allocated strings */
        H5Dvlen_reclaim(stype, space, H5P_DEFAULT, keys);
        free(keys); free(vals);
        H5Tclose(stype); H5Sclose(space);
        H5Dclose(dset_k); H5Dclose(dset_v);
        H5Gclose(gid); H5Fclose(file);
        return -1;
    }

    /* clear store and populate */
    /* Reuse metadata_load_json clearing behaviour: but we will remove directly */
    /* clear existing */
    /* (we know metadata API provides metadata_set; we will remove entries by reading) */
    /* Destroy existing contents */
    /* We'll re-create store contents from HDF5 read */
    /* First clear store */
    /* Reuse store by removing all current entries: simplest is to destroy and create new one.
     * But we cannot replace caller's pointer. So remove all keys by iterating.
     */
    /* Build store fresh: remove all existing entries */
    /* iterate and collect keys to remove */
    size_t removed = 0;
    /* remove everything by iterating and storing keys */
    /* Simple approach: while count > 0, remove head by accessing internal API is not available.
     * Instead, we can serialize to JSON and reload. But that would be roundabout.
     * For simplicity we will remove by reading keys from another pass. We'll create a temporary
     * store and swap internal pointers via destroy/create is not allowed.
     *
     * Simpler: call metadata_load_json with an empty string "{}" to clear it, but metadata_load_json
     * treats that specially. We will simulate clear by calling metadata_load_json("{}").
     */
    metadata_load_json(store, "{}");

    for (size_t i = 0; i < n; ++i) {
        if (keys[i]) {
            metadata_set(store, keys[i], vals[i] ? vals[i] : "");
        }
    }

    /* Reclaim variable-length memory allocated by HDF5 */
    H5Dvlen_reclaim(stype, space, H5P_DEFAULT, keys);
    H5Dvlen_reclaim(stype, space, H5P_DEFAULT, vals);

    free(keys);
    free(vals);
    H5Tclose(stype);
    H5Sclose(space);
    H5Dclose(dset_k);
    H5Dclose(dset_v);
    H5Gclose(gid);
    H5Fclose(file);
    return 0;
}

int hdf5_index_exists(const char *target_filepath) {
    if (!target_filepath) return -1;
    hid_t file = H5Fopen(target_filepath, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return 0;
    /* check for /index group */
    htri_t exists = H5Lexists(file, "/index", H5P_DEFAULT);
    H5Fclose(file);
    return (exists > 0) ? 1 : 0;
}

#endif /* HAVE_HDF5 */
