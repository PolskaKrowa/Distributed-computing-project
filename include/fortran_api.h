#ifndef FORTRAN_API_H
#define FORTRAN_API_H

#include <stdint.h>
#include <stddef.h>

/*
 * Distributed Computing Project - Fortran API
 * 
 * Canonical C-callable interface for Fortran computational kernels.
 * 
 * This is the only permitted boundary between C and Fortran code.
 * All Fortran entrypoints must follow the signature defined here.
 * 
 * Memory contract:
 * - All buffers are owned by C runtime
 * - Fortran may read input_buf and write to output_buf
 * - Fortran must NOT allocate or free these buffers
 * - Fortran must NOT exceed output_size bytes
 * 
 * Threading contract:
 * - Fortran entrypoints are NOT assumed thread-safe
 * - C runtime must serialize calls unless documented otherwise
 * 
 * Error handling:
 * - Return 0 for success
 * - Return positive error codes from errcodes.h for failures
 * - Optionally write diagnostic to out_status_code
 * - NEVER call exit() or stop from Fortran
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical Fortran model execution signature (version 1)
 *
 * This is the standard interface all Fortran models must implement.
 * The symbol name must be exactly: fortran_model_run_v1
 * 
 * Parameters:
 *   input_buf      - pointer to input data (read-only)
 *   input_size     - size of input in bytes
 *   output_buf     - pointer to output buffer (write-only)
 *   output_size    - available space in output buffer (bytes)
 *   meta_buf       - optional metadata (TLV format), may be NULL
 *   meta_size      - size of metadata in bytes, 0 if meta_buf is NULL
 *   trace_id       - correlation ID for logging
 *   out_status_code - optional pointer for detailed status, may be NULL
 * 
 * Returns:
 *   0 (OK) on success
 *   positive error code (see errcodes.h) on failure
 * 
 * Example Fortran implementation:
 *
 * subroutine fortran_model_run_v1(input, input_bytes, output, output_bytes, &
 *                                  meta, meta_bytes, trace_id, out_status) &
 *                                  bind(C, name="fortran_model_run_v1")
 *     use iso_c_binding
 *     implicit none
 *     type(c_ptr), value :: input, output, meta
 *     integer(c_size_t), value :: input_bytes, output_bytes, meta_bytes
 *     integer(c_int64_t), value :: trace_id
 *     integer(c_int32_t), intent(out) :: out_status
 *     
 *     ! Use c_f_pointer to convert C pointers to Fortran arrays
 *     ! Perform computation
 *     ! Write results to output buffer
 *     ! Set out_status = 0 for success
 * end subroutine
 */
int32_t fortran_model_run_v1(
    const void *input_buf,
    size_t input_size,
    void *output_buf,
    size_t output_size,
    const void *meta_buf,
    size_t meta_size,
    uint64_t trace_id,
    int32_t *out_status_code
);

/* Fortran state serialization for checkpointing (version 1)
 *
 * Optional interface for models that support checkpointing.
 * If not implemented, checkpointing is handled entirely by C runtime.
 * 
 * Parameters:
 *   state_in       - pointer to current state data
 *   state_in_size  - size of state data in bytes
 *   buffer_out     - pointer to output buffer for serialized state
 *   buffer_size    - available space in buffer
 *   bytes_written  - pointer to receive actual bytes written
 *   trace_id       - correlation ID for logging
 * 
 * Returns:
 *   0 (OK) on success
 *   ERR_BUFFER_TOO_SMALL if buffer is insufficient
 *   other error code on failure
 */
int32_t fortran_serialize_state_v1(
    const void *state_in,
    size_t state_in_size,
    void *buffer_out,
    size_t buffer_size,
    size_t *bytes_written,
    uint64_t trace_id
);

/* Fortran state deserialization for checkpoint restoration (version 1)
 *
 * Companion to serialize_state; restores state from checkpoint.
 * 
 * Parameters:
 *   buffer_in      - pointer to serialized state data
 *   buffer_size    - size of serialized data in bytes
 *   state_out      - pointer to output buffer for restored state
 *   state_out_size - available space for restored state
 *   trace_id       - correlation ID for logging
 * 
 * Returns:
 *   0 (OK) on success
 *   error code on failure
 */
int32_t fortran_deserialize_state_v1(
    const void *buffer_in,
    size_t buffer_size,
    void *state_out,
    size_t state_out_size,
    uint64_t trace_id
);

/* Model metadata query (version 1)
 *
 * Optional interface for models to report their requirements.
 * 
 * Parameters:
 *   model_id       - identifier for the model being queried
 *   info_buf       - pointer to output buffer for model info (TLV format)
 *   info_buf_size  - available space in info buffer
 *   bytes_written  - pointer to receive actual bytes written
 * 
 * Returns:
 *   0 (OK) on success
 *   error code on failure
 * 
 * Expected TLV records in output:
 *   - Input size requirements
 *   - Output size requirements
 *   - Memory requirements
 *   - Thread safety info
 */
int32_t fortran_model_query_v1(
    uint32_t model_id,
    void *info_buf,
    size_t info_buf_size,
    size_t *bytes_written
);

#ifdef __cplusplus
}
#endif

#endif /* FORTRAN_API_H */