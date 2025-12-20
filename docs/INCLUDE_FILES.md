# Include Files Documentation

This document provides comprehensive documentation for all header files in the `include/` and `src/common/` directories of the Distributed Computing Project.

---

## Overview

The project uses a strict separation between C and Fortran code. All interaction between the two languages happens through a small, explicit C ABI defined in these header files.

**Key principles:**
- Headers in `include/` define the public, stable API
- Headers in `src/common/` provide shared utilities for C code
- Only files in `include/fortran_api.h` define the C↔Fortran boundary
- All interfaces are versioned and backwards-compatible

---

## Public API Headers (`include/`)

### project.h

**Purpose:** Project-wide definitions, version information, and constants.

**Key contents:**

| Definition | Description |
|------------|-------------|
| `PROJECT_VERSION_*` | Semantic version numbers (MAJOR.MINOR.PATCH) |
| `API_VERSION_CURRENT` | Current API version for C↔Fortran boundary |
| `MESSAGE_MAGIC` | Magic number for message validation (0xDA7A1E00) |
| `CHECKPOINT_MAGIC` | Magic number for checkpoint validation (0xC8EC7001) |
| `MAX_WORKERS` | Maximum number of concurrent workers (1024) |
| `MAX_TASK_QUEUE_SIZE` | Maximum tasks in queue (65536) |
| `MAX_MODEL_NAME_LEN` | Maximum length for model names (256) |
| `MAX_DIAG_MSG_LEN` | Maximum length for diagnostic messages (1024) |
| `TLV_TYPE_*` | Type codes for TLV metadata format |
| `TASK_FLAG_*` | Bitflags for task behaviour options |
| `message_type_t` | Enum of message types for transport layer |

**Usage:**
```c
#include "project.h"

// Check API version compatibility
if (task->api_version != API_VERSION_CURRENT) {
    log_warn("API version mismatch: got %d, expected %d",
             task->api_version, API_VERSION_CURRENT);
}

// Set task flags
task->flags = TASK_FLAG_CHECKPOINT_ENABLED | TASK_FLAG_HIGH_PRIORITY;
```

---

### task.h

**Purpose:** Defines the canonical task structure used throughout the distributed system.

**Key contents:**

#### `task_t` structure

The central data structure for all distributed computation. Created by coordinators, transmitted to workers, and used to invoke Fortran kernels.

| Field | Type | Description |
|-------|------|-------------|
| `task_id` | `uint64_t` | Unique identifier across the entire run |
| `model_id` | `uint32_t` | Selects which Fortran kernel to execute |
| `input` | `const void*` | Pointer to input data buffer (read-only for Fortran) |
| `input_size` | `size_t` | Size of input buffer in bytes |
| `output` | `void*` | Pointer to output buffer (write-only for Fortran) |
| `output_size` | `size_t` | Available space in output buffer (bytes) |
| `meta` | `const void*` | Optional metadata buffer (TLV format) |
| `meta_size` | `size_t` | Size of metadata in bytes |
| `trace_id` | `uint64_t` | Correlation ID for logging and tracing |
| `api_version` | `uint32_t` | API version for interface validation |
| `flags` | `uint32_t` | Optional behaviour flags |
| `timeout_secs` | `int32_t` | Timeout in seconds (≤0 = no timeout) |

**Memory ownership rules:**
- All buffers are owned by C runtime
- Fortran may read `input` and write `output` but MUST NOT free them
- Fortran MUST NOT exceed `output_size` bytes

#### `task_state_t` enum

Represents the lifecycle state of a task.

| State | Description |
|-------|-------------|
| `TASK_STATE_QUEUED` | Task created but not yet dispatched |
| `TASK_STATE_DISPATCHED` | Task sent to worker but not started |
| `TASK_STATE_RUNNING` | Task currently executing |
| `TASK_STATE_COMPLETED` | Task finished successfully |
| `TASK_STATE_FAILED` | Task failed with error |
| `TASK_STATE_TIMEOUT` | Task exceeded timeout limit |
| `TASK_STATE_CANCELLED` | Task cancelled by coordinator |

**Usage:**
```c
#include "task.h"

// Create a task
task_t task = {
    .task_id = next_task_id++,
    .model_id = MODEL_HARMONIC_OSCILLATOR,
    .input = input_buffer,
    .input_size = sizeof(double) * 100,
    .output = output_buffer,
    .output_size = sizeof(double) * 200,
    .meta = NULL,
    .meta_size = 0,
    .trace_id = generate_trace_id(),
    .api_version = API_VERSION_CURRENT,
    .flags = TASK_FLAG_CHECKPOINT_ENABLED,
    .timeout_secs = 300
};
```

---

### errcodes.h

**Purpose:** Defines standard error codes returned by all functions.

**Key contents:**

#### `errcode_t` enum

| Code | Value | Description |
|------|-------|-------------|
| `OK` | 0 | Success |
| `ERR_INVALID_ARGUMENT` | 1 | Invalid function argument |
| `ERR_BUFFER_TOO_SMALL` | 2 | Provided buffer insufficient |
| `ERR_COMPUTATION_FAILED` | 3 | Numerical computation error |
| `ERR_TIMEOUT` | 4 | Operation exceeded timeout |
| `ERR_NOT_IMPLEMENTED` | 5 | Feature not yet implemented |

**Error handling contract:**
- Every cross-language call returns `int32_t` status
- Zero (`OK`) means success
- Positive values are documented error codes
- Negative values reserved for fatal/internal errors
- Functions MUST NOT use `exit()` or `stop` - return error codes instead

**Usage:**
```c
#include "errcodes.h"

errcode_t rc = fortran_model_run_v1(...);
if (rc != OK) {
    log_error("Task %lu failed with error code %d", task->task_id, rc);
    // Handle error appropriately
}
```

---

### result.h

**Purpose:** Defines structures for task results and execution status.

**Key contents:**

#### `task_result_t` structure

Returned by workers after task completion.

| Field | Type | Description |
|-------|------|-------------|
| `task_id` | `uint64_t` | Task identifier (matches `task_t.task_id`) |
| `status` | `errcode_t` | Execution status from `errcodes.h` |
| `output` | `void*` | Pointer to output data |
| `output_bytes_written` | `size_t` | Actual bytes written to output |
| `exec_time_us` | `uint64_t` | Execution time in microseconds |
| `queue_time_us` | `uint64_t` | Time spent in queue (microseconds) |
| `worker_id` | `uint32_t` | Identifier of worker that executed task |
| `diag_msg` | `char[1024]` | Optional diagnostic message (null-terminated) |

#### Helper functions

**`status_to_string(status_t status)`**
- Converts status code to human-readable string
- Returns: `const char*` string representation
- Useful for logging and error reporting

**Usage:**
```c
#include "result.h"

task_result_t result;
// ... populate result ...

log_info("Task %lu completed: %s (exec=%lu us, queue=%lu us)",
         result.task_id,
         status_to_string(result.status),
         result.exec_time_us,
         result.queue_time_us);
```

---

### fortran_api.h

**Purpose:** Defines the canonical C-callable interface for Fortran computational kernels. This is the **only permitted boundary** between C and Fortran code.

**Key contents:**

#### `fortran_model_run_v1()`

The standard interface all Fortran models must implement.

**Signature:**
```c
int32_t fortran_model_run_v1(
    const void *input_buf,      // Input data (read-only)
    size_t input_size,          // Input size in bytes
    void *output_buf,           // Output buffer (write-only)
    size_t output_size,         // Output buffer size in bytes
    const void *meta_buf,       // Optional metadata (TLV format)
    size_t meta_size,           // Metadata size (0 if NULL)
    uint64_t trace_id,          // Logging correlation ID
    int32_t *out_status_code    // Optional detailed status
);
```

**Parameters:**
- `input_buf`: Pointer to input data (read-only for Fortran)
- `input_size`: Size of input in bytes
- `output_buf`: Pointer to output buffer (write-only for Fortran)
- `output_size`: Available space in output buffer (bytes)
- `meta_buf`: Optional metadata in TLV format, may be NULL
- `meta_size`: Size of metadata (0 if `meta_buf` is NULL)
- `trace_id`: Correlation ID for logging
- `out_status_code`: Optional pointer for detailed status (may be NULL)

**Returns:**
- `0` (`OK`) on success
- Positive error code (see `errcodes.h`) on failure

**Memory contract:**
- All buffers owned by C runtime
- Fortran may read `input_buf` and write `output_buf`
- Fortran MUST NOT allocate or free these buffers
- Fortran MUST NOT exceed `output_size` bytes

**Threading contract:**
- Fortran entrypoints NOT assumed thread-safe
- C runtime must serialize calls unless documented otherwise

**Error handling:**
- Return error codes, never call `exit()` or `stop`
- Optionally write diagnostic to `out_status_code`

**Fortran implementation example:**
```fortran
subroutine fortran_model_run_v1(input, input_bytes, output, output_bytes, &
                                 meta, meta_bytes, trace_id, out_status) &
                                 bind(C, name="fortran_model_run_v1")
    use iso_c_binding
    implicit none
    type(c_ptr), value :: input, output, meta
    integer(c_size_t), value :: input_bytes, output_bytes, meta_bytes
    integer(c_int64_t), value :: trace_id
    integer(c_int32_t), intent(out) :: out_status
    
    ! Use c_f_pointer to convert C pointers to Fortran arrays
    ! Perform computation
    ! Write results to output buffer
    out_status = 0  ! Success
end subroutine
```

#### `fortran_serialize_state_v1()`

Optional interface for models that support checkpointing.

**Signature:**
```c
int32_t fortran_serialize_state_v1(
    const void *state_in,
    size_t state_in_size,
    void *buffer_out,
    size_t buffer_size,
    size_t *bytes_written,
    uint64_t trace_id
);
```

**Purpose:** Serializes internal Fortran state for checkpointing.

**Returns:**
- `OK` on success
- `ERR_BUFFER_TOO_SMALL` if buffer insufficient
- Other error codes on failure

#### `fortran_deserialize_state_v1()`

Companion to `serialize_state`; restores state from checkpoint.

**Signature:**
```c
int32_t fortran_deserialize_state_v1(
    const void *buffer_in,
    size_t buffer_size,
    void *state_out,
    size_t state_out_size,
    uint64_t trace_id
);
```

#### `fortran_model_query_v1()`

Optional interface for models to report their requirements.

**Signature:**
```c
int32_t fortran_model_query_v1(
    uint32_t model_id,
    void *info_buf,
    size_t info_buf_size,
    size_t *bytes_written
);
```

**Purpose:** Query model metadata (input/output sizes, memory requirements, thread safety).

**Output:** TLV-encoded model information in `info_buf`.

---

## Common Utility Headers (`src/common/`)

### log.h

**Purpose:** Unified logging system for debugging and error reporting.

**Key contents:**

#### `log_level_t` enum

| Level | Description |
|-------|-------------|
| `LOG_LEVEL_DEBUG` | Detailed debugging information |
| `LOG_LEVEL_INFO` | General informational messages |
| `LOG_LEVEL_WARN` | Warning messages (degraded but functional) |
| `LOG_LEVEL_ERROR` | Error messages (feature unavailable) |
| `LOG_LEVEL_FATAL` | Fatal errors (program cannot continue) |

#### Functions

**`log_init(const char *path, log_level_t level)`**
- Initializes logging system (call once at startup)
- `path`: Log file path, NULL for default "program.log"
- `level`: Minimum level to record
- Returns: 0 on success, -1 on failure

**`log_shutdown(void)`**
- Flushes and closes log file
- MUST be called before program exit

#### Logging macros

- `log_debug(fmt, ...)` - Debug messages
- `log_info(fmt, ...)` - Informational messages
- `log_warn(fmt, ...)` - Warnings
- `log_error(fmt, ...)` - Errors
- `log_fatal(fmt, ...)` - Fatal errors (calls `abort()`)

**Usage rules:**
- Log messages MUST identify which module is logging
- Avoid ambiguous messages like "Function started"
- Use descriptive messages: "Task %ld created (size=%zu, priority=%d)"
- This is the ONLY permitted logging mechanism in the project

**Usage example:**
```c
#include "common/log.h"

int main(void) {
    if (log_init("runtime.log", LOG_LEVEL_DEBUG) != 0) {
        return 1;
    }

    log_info("Runtime started (version %d.%d.%d)",
             PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH);
    log_debug("Worker count = %d", worker_count);
    log_warn("Slow network response detected");
    log_error("Failed to open socket: %s", strerror(errno));

    log_shutdown();
    return 0;
}
```

---

### errors.h

**Purpose:** Automatic crash detection and reporting system.

**Key contents:**

#### Functions

**`error_reporter_install(const char *report_url, const char *log_path)`**
- Installs signal handlers for crash detection
- `report_url`: Optional endpoint for crash report upload (may be NULL)
- `log_path`: Path to log file to include in crash report
- Returns: 0 on success

**Behaviour:**
- Catches signals: SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS
- Captures stack trace (on supported platforms)
- Includes tail of log file in crash report
- Prompts user to submit report (if interactive)
- Saves crash report to `/tmp/crash-report-<pid>-<timestamp>.log`

**`error_reporter_uninstall(void)`**
- Removes signal handlers (optional, mainly for tests)

**Usage:**
```c
#include "common/errors.h"
#include "common/log.h"

int main(void) {
    log_init("runtime.log", LOG_LEVEL_DEBUG);
    
    // Install immediately after log_init
    error_reporter_install("https://crash.example.com/upload", "runtime.log");
    
    // Rest of program...
    // Crashes are automatically caught and reported
    
    return 0;
}
```

**Integration with log.h:**
- Should be installed immediately after `log_init()`
- Includes log tail in crash reports
- Uses `log_error()` to record crash events

---

## Usage Guidelines

### For C developers

1. **Always include required headers:**
   ```c
   #include "project.h"   // For constants and version
   #include "task.h"      // For task structures
   #include "errcodes.h"  // For error codes
   #include "common/log.h"  // For logging
   ```

2. **Initialize logging first:**
   ```c
   log_init("runtime.log", LOG_LEVEL_DEBUG);
   error_reporter_install("https://...", "runtime.log");
   ```

3. **Always check error codes:**
   ```c
   errcode_t rc = some_function();
   if (rc != OK) {
       log_error("Operation failed: %s", status_to_string(rc));
       return rc;
   }
   ```

4. **Validate API versions:**
   ```c
   if (task->api_version != API_VERSION_CURRENT) {
       return ERR_INVALID_ARGUMENT;
   }
   ```

### For Fortran developers

1. **Implement the canonical interface:**
   - Use `bind(C, name="fortran_model_run_v1")` exactly
   - Never allocate or free C-provided buffers
   - Return error codes, never call `stop` or `exit()`

2. **Convert C pointers to Fortran arrays:**
   ```fortran
   use iso_c_binding
   real(c_double), pointer :: input_array(:)
   call c_f_pointer(input, input_array, [input_bytes/c_sizeof(1.0_c_double)])
   ```

3. **Respect buffer sizes:**
   - Check `output_size` before writing
   - Return `ERR_BUFFER_TOO_SMALL` if insufficient

### For reviewers

Checklist for code review:

- [ ] `bind(C)` names match header exactly
- [ ] No buffer allocation/deallocation in Fortran
- [ ] API version checked before assuming behaviour
- [ ] Error codes returned, not `exit()` called
- [ ] Logging uses provided macros
- [ ] Memory ownership clearly documented
- [ ] Thread safety documented if applicable

---

## Version History

### API Version 1 (Current)

- Initial stable API
- Canonical task structure (`task_t`)
- Standard Fortran interface (`fortran_model_run_v1`)
- Basic error codes
- Optional checkpointing support

### Future versions

When making incompatible changes:
1. Increment API version number
2. Create new function with version suffix (e.g., `fortran_model_run_v2`)
3. Keep old version for backwards compatibility
4. Document migration path

---

## Common Pitfalls

1. **Buffer overflow in Fortran:**
   - Always check `output_size` before writing
   - Use `ERR_BUFFER_TOO_SMALL` if insufficient space

2. **Memory leaks:**
   - C owns all buffers - never free them in Fortran
   - Fortran owns internal state - always deallocate before returning

3. **Thread safety:**
   - Assume Fortran is NOT thread-safe
   - Serialize calls from C unless documented otherwise

4. **API version mismatches:**
   - Always validate `task->api_version`
   - Handle version differences gracefully

5. **Logging without context:**
   - Bad: `log_debug("Processing started")`
   - Good: `log_debug("Task %lu processing started (model=%d)", task_id, model_id)`

---

## Quick Reference

### Include order (recommended)

```c
// Standard library
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Project headers
#include "project.h"
#include "errcodes.h"
#include "task.h"
#include "result.h"
#include "fortran_api.h"

// Common utilities
#include "common/log.h"
#include "common/errors.h"

// Module-specific headers
#include "scheduler.h"
```

### Minimal working example

```c
#include "project.h"
#include "task.h"
#include "fortran_api.h"
#include "common/log.h"
#include "common/errors.h"

int main(void) {
    // Setup
    log_init("worker.log", LOG_LEVEL_DEBUG);
    error_reporter_install(NULL, "worker.log");
    
    // Create task
    task_t task = {
        .task_id = 1,
        .model_id = 100,
        .input = input_data,
        .input_size = sizeof(input_data),
        .output = output_data,
        .output_size = sizeof(output_data),
        .api_version = API_VERSION_CURRENT,
        .trace_id = 1000
    };
    
    // Execute
    int32_t rc = fortran_model_run_v1(
        task.input, task.input_size,
        task.output, task.output_size,
        NULL, 0,
        task.trace_id,
        NULL
    );
    
    if (rc != OK) {
        log_error("Task failed: %s", status_to_string(rc));
        return 1;
    }
    
    log_info("Task completed successfully");
    
    // Cleanup
    log_shutdown();
    return 0;
}
```

---

## Additional Resources

- See `docs/architecture.md` for system design overview
- See `docs/fortran_guidelines.md` for Fortran coding standards
- See `docs/c_guidelines.md` for C coding standards
- See `docs/numerical_reproducibility.md` for numerical correctness guidelines

---

**Last updated:** 2024-12-20