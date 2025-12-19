#ifndef HDF5_INDEX_H
#define HDF5_INDEX_H

#include "metadata.h"

/* Write an index for the target file path.
 *
 * If compiled with -DHAVE_HDF5 and linked to HDF5, this writes a small HDF5
 * dataset containing the metadata. Otherwise it writes a JSON sidecar file
 * with the suffix ".index.json" next to the provided filepath.
 *
 * Returns 0 on success, non-zero on error.
 */
int hdf5_index_write(const char *target_filepath, const metadata_store_t *store);

/* Read index back into the provided metadata_store. The store is cleared
 * before loading. Returns 0 on success, non-zero on error. */
int hdf5_index_read(const char *target_filepath, metadata_store_t *store);

/* Returns 1 if an index exists for the target filepath (HDF5 dataset or json
 * sidecar), 0 if not, negative on error. */
int hdf5_index_exists(const char *target_filepath);

#endif /* HDF5_INDEX_H */
