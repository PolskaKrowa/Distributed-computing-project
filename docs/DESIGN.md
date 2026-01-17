# Project Overview and Developer Guide

## 1. Purpose of the Project

This project is a **distributed scientific computing platform** designed to support **amateur and independent research in physics and mathematics**.

Its goals are:

- To make large-scale numerical computation accessible without requiring specialist HPC infrastructure
- To provide a clear separation between scientific code and distributed systems code
- To prioritise correctness, reproducibility, and long-term maintainability
- To remain approachable for contributors with strong maths or physics backgrounds but limited systems experience

The project is written in **Fortran and C**, using each language where it is technically and culturally strongest.

- **Fortran** is used for numerical computation and scientific models
- **C** is used for distributed execution, scheduling, networking, and runtime control

This separation is strict and intentional.

---

## 2. Design Philosophy

### Core principles

- Fortran does maths, C does systems
- Scientific code must be deterministic and reproducible
- Distributed code must be robust against failure
- Interfaces between languages must be minimal and explicit
- Contributors should be able to work in one language without understanding the other
- Linux only for simplicity

### Non-goals

- This is not a general-purpose cloud framework
- This is not a web service
- This is not an object-oriented abstraction-heavy system

---

## 3. Full Project Structure

```

Distributed computing project/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── CONTRIBUTING.md
│
├── docs/
│   ├── architecture.md
│   ├── fortran_guidelines.md
│   ├── c_guidelines.md
│   └── numerical_reproducibility.md
│
├── cmake/
│   ├── FindBLAS.cmake
│   ├── FindLAPACK.cmake
│   ├── FindMPI.cmake
│   └── CompilerWarnings.cmake
│
├── include/
│   ├── project.h
│   ├── errcodes.h
│   ├── task.h
│   ├── result.h
│   └── fortran_api.h
│
├── src/
│   ├── coordinator/
│   │   ├── main.c
│   │   ├── scheduler.c
│   │   ├── scheduler.h
│   │   ├── worker_registry.c
│   │   └── worker_registry.h
│   │
│   ├── worker/
│   │   ├── main.c
│   │   ├── executor.c
│   │   ├── executor.h
│   │   ├── checkpoint.c
│   │   └── checkpoint.h
│   │
│   ├── net/
│   │   ├── transport_mpi.c
│   │   ├── transport_zmq.c
│   │   └── transport.h
│   │
│   ├── storage/
│   │   ├── metadata.c
│   │   ├── metadata.h
│   │   ├── hdf5_index.c
│   │   └── hdf5_index.h
│   │
│   ├── runtime/
│   │   ├── task_queue.c
│   │   ├── task_queue.h
│   │   ├── resource_limits.c
│   │   └── resource_limits.h
│   │
│   └── common/
│       ├── time_utils.h
│       ├── errors.h
│       ├── errors.c
│       ├── log.c
│       └── log.h
│
├── fortran/
│   ├── core/
│   │   ├── kinds.f90
│   │   ├── constants.f90
│   │   ├── rng.f90
│   │   └── numerics_utils.f90
│   │
│   ├── kernels/
│   │   ├── linear_algebra/
│   │   │   ├── solve_linear.f90
│   │   │   ├── dense_matrix.f90
│   │   │   ├── sparse_matrix.f90
│   │   │   └── eigen.f90
│   │   │
│   │   ├── pde/
│   │   │   ├── spectral.f90
│   │   │   ├── finite_difference.f90
│   │   │   └── finite_volume.f90
│   │   │
│   │   ├── ode/
│   │   │   ├── dormand_prince.f90
│   │   │   ├── rk4.f90
│   │   │   └── backward_euler.f90
│   │   │
│   │   ├── optimisation/
│   │   │   ├── gradient_descent.f90
│   │   │   ├── conjugate_gradient.f90
│   │   │   ├── quasi_newton.f90
│   │   │   └── constrained.f90
│   │   │
│   │   ├── root_find/
│   │   │   ├── newton_rhapson.f90
│   │   │   └── secant.f90
│   │   │
│   │   ├── statistics/
│   │   │   ├── monte_carlo.f90
│   │   │   ├── markov_chain.f90
│   │   │   ├── boostrap.f90
│   │   │   ├── resampling.f90
│   │   │   └── bayesian_inference.f90
│   │   │
│   │   └── convolution/
│   │       ├── multidim_fft.f90
│   │       ├── real_to_complex.f90
│   │       ├── complex_to_complex.f90
│   │       ├── convolution.f90
│   │       └── correlation.f90
│   │
│   ├── models/
│   │   ├── ode/
│   │   │   ├── harmonic_oscilator.f90
│   │   │   ├── pendulum.f90
│   │   │   ├── coupled_oscilators.f90
│   │   │   ├── lorenz.f90
│   │   │   ├── van_der_pol_oscillator.f90
│   │   │   └── lotka_volterra.f90
│   │   │
│   │   ├── classical_mechanics/
│   │   │   ├── nbody_gravitation.f90
│   │   │   └── rigidbody_dynamics.f90
│   │   │
│   │   ├── field_continuum/
│   │   │   ├── diffusion_heat_equation.f90
│   │   │   ├── wave_equation.f90
│   │   │   └── advection_diffusion.f90
│   │   │
│   │   ├── fluid_dynamics/
│   │   │   ├── incompressibles.f90
│   │   │   └── lattice_boltzmann.f90
│   │   │
│   │   ├── electromagnetism_waves/
│   │   │   ├── maxwell.f90
│   │   │   ├── poisson.f90
│   │   │   └── laplace.f90
│   │   │
│   │   ├── statistical_stochastic/
│   │   │   ├── ising.f90
│   │   │   └── random_walk.f90
│   │   │
│   │   ├── biology_ecology/
│   │   │   ├── population_dynamics.f90
│   │   │   └── neural_networking.f90
│   │   │
│   │   └── materials
│   │       ├── allen_cahn.f90
│   │       └── cahn_hilliard.f90
│   │
│   ├── io/
│   │   ├── write_results.f90
│   │   └── read_input.f90
│   │
│   └── interface/
│       ├── c_api.f90
│       └── model_registry.f90
│
├── tests/
│   ├── fortran/
│   │   ├── test_linear_algebra.f90
│   │   └── test_rng.f90
│   │
│   ├── c/
│   │   ├── test_scheduler.c
│   │   └── test_queue.c
│   │
│   └── integration/
│       └── test_distributed_run.c
│
├── examples/
│   ├── minimal_cluster/
│   ├── volunteer_computing/
│   └── parameter_sweep/
│
├── tools/
│   ├── cli/
│   │   └── projectctl.c
│   ├── converters/
│   └── diagnostics/
│
└── data/
    ├── schemas/
    ├── sample_inputs/
    └── reference_outputs/

```

---

## 4. Responsibilities by Language

### C responsibilities

C code is responsible for:

- Distributed scheduling and orchestration
- Worker lifecycle management
- Networking and message passing
- Resource limits and fault handling
- Task queues and execution control
- Metadata and indexing

C code must not perform numerical computation beyond trivial bookkeeping.

---

### Fortran responsibilities

Fortran code is responsible for:

- Numerical algorithms
- Mathematical kernels
- Physics and mathematical models
- Deterministic computation
- Scientific data output

Fortran code must not:

- Perform networking
- Manage threads or processes
- Perform distributed coordination
- Access the operating system directly

---

## 5. How Source Files Interact

### Language boundary

There is exactly one allowed boundary between C and Fortran:

```

C runtime  <->  fortran/interface

```

All interaction happens through **C-compatible function calls** defined using `bind(C)`.

No other cross-language dependencies are allowed.

---

### Control flow

1. C code receives or schedules a task
2. C prepares input data in plain memory buffers
3. C calls a Fortran entry point
4. Fortran performs computation only
5. Fortran returns results via output buffers
6. C handles storage, transmission, or aggregation

Fortran never calls back into C.

---

### Data exchange rules

- Use plain arrays and structs
- No Fortran derived types across the boundary
- No pointers shared across languages
- Explicit sizes and strides only
- Memory ownership is always defined by C

---

## 6. Writing Fortran Code

### Directory intent

- `core/`: fundamental utilities, kinds, constants
- `kernels/`: reusable numerical building blocks
- `models/`: domain-specific scientific implementations
- `io/`: scientific file output only
- `interface/`: C-callable entry points only

---

### Fortran coding rules

- Use `implicit none` everywhere
- Use modern Fortran standards (2008 or newer)
- Prefer pure and elemental procedures where possible
- No global mutable state
- No random behaviour without explicit seeding
- No side effects outside provided buffers
- All public interfaces must be documented

---

### Interface rules

- Only files in `fortran/interface/` may use `bind(C)`
- One C-callable routine per logical task
- All interfaces must be stable and versioned
- Do not expose internal modules to C

---

## 7. Writing C Code

### Directory intent

- `coordinator/`: master scheduling logic
- `worker/`: execution and checkpointing
- `net/`: transport backends
- `runtime/`: queues, limits, lifecycle
- `storage/`: metadata and indexing
- `common/`: logging, errors, utilities

---

### C coding rules

- C99 minimum
- No compiler extensions unless guarded
- No global mutable state across modules
- Explicit ownership of all allocations
- All error paths must be handled
- No numerical algorithms in C

---

### ABI rules

- All Fortran calls must go through headers in `include/`
- No inline knowledge of Fortran internals
- Do not assume Fortran memory layout beyond the interface contract

---

## 8. Build and Configuration

- CMake is the only supported build system
- All dependencies must be detected automatically
- Mixed-language targets must be explicit
- Warnings are treated seriously

---

## 9. Testing Expectations

- Fortran kernels must have unit tests
- C runtime components must have unit tests
- Integration tests must cover distributed execution
- Tests must be deterministic and reproducible

---

## 10. Contributor Expectations

Contributors are expected to:

- Follow the language separation rules strictly
- Document public interfaces
- Write tests for new functionality
- Avoid cleverness in favour of clarity
- Treat numerical correctness as a first-class concern

---

## 11. Long-term Stability

This project is designed to remain usable for decades.

- Interfaces are conservative
- Dependencies are mature
- Language choices prioritise longevity
- Backwards compatibility is valued over novelty

If in doubt, choose the simplest solution that can be understood by a physicist reading the code five years from now.

---

## 12. Common code

Common source files are provided to ease development and reduce clutter.

### Shared C files

#### log.h

This is a provided logging library that has been written to ease debugging and
allow for quick bug reporting.

Using this library is as simple as pie:
```c
#include "common/log.h"

int main(void)
{
    // initialise the logging function (USE ONLY ONCE IN EACH MAIN SCRIPT)
    if (log_init("runtime.log", LOG_LEVEL_DEBUG) != 0) {
        return 1;
    }

    log_info("runtime started");            // information (useful for stating any information that may be useful for the user)
    log_debug("worker count = %d", 8);      // debugging data (same as info but with built-in string formatting to show data)
    log_warn("slow network response");      // warning (only displayed if runtime is negatively affected but not strictly disasterous)
    log_error("failed to open socket");     // error (only displayed if runtime is critically affected from a bug/hardware issue and certain features are unavailable)
    log_fatal("unable to allocate memory"); // fatal error (only displayed if runtime is unable to continue running and must exit immediately)

    log_shutdown(); // shut down ALL LOGS (This MUST be the last function called before program exit.)
    return 0;
}
```

#### errors.h

This is an automatic crash reporter library to allow for users to quickly report crashes that occur.

Using this library is even easier than log.h:
```c
#include "common/errors.h"

int main(void)
{
    // other initialisation functions, including log_init(...)

    // Install reporter; optional report URL and log path
    // Preferrably place this right after log_init to capture any crashes caused by log.h
    error_reporter_install("https://your.crash.endpoint/upload", "runtime.log");

    // rest of program...

    return 0;
}
```


# API and Function Contracts

This segment extends the above documentation by standardising function signatures, data contracts, and interaction patterns between components and languages. It is written so that each developer can implement their piece without seeing other developers' code while still producing compatible behaviour.

> Refer to the base design for project scope and language separation. This supplementary document contains concrete contracts and examples for implementers.

---

## 1. Goals of these contracts

1. Make cross-module integration deterministic and low friction.
2. Minimise accidental undefined behaviour at the language boundary.
3. Keep interfaces stable and versioned so independent copies remain compatible.
4. Provide explicit ownership, threading and error semantics.

---

## 2. Versioning and symbol stability

1. All cross-language entry points must include a semantic version in their exported symbol name, for example `model_run_v1`.
2. Minor ABI-compatible changes may increment a minor suffix: `model_run_v1_1`.
3. Never rename an exported symbol; provide a new symbol instead and keep old symbols for backwards compatibility.

---

## 3. Memory ownership and buffer rules

1. C always owns heap memory. Fortran may read/write buffers provided by C but must not free them. If Fortran must allocate, it must return a plain pointer and document the ownership semantics explicitly.
2. All buffers passed across the boundary must be simple contiguous arrays. Use explicit lengths and element sizes.
3. No Fortran derived types cross the boundary. No C pointers are embedded in Fortran-only data structures that C will access.
4. Alignment and packing must be standard C ABI. Avoid platform-specific assumptions.

---

## 4. Threading and reentrancy rules

1. Fortran entrypoints are not assumed to be thread safe unless explicitly documented as thread safe in the interface version notes.
2. The C runtime must serialise concurrent calls into a single Fortran library unless the Fortran module explicitly documents thread safety.
3. If parallel execution of the same Fortran library is required, prefer process isolation (separate worker processes) rather than concurrent in-process calls.
4. C modules may be multi-threaded. Any C function that calls a Fortran entrypoint must follow the serialisation policy.

---

## 5. Error handling contract

1. Every cross-language call returns an integer status code. Zero means success. Positive values are recognised error codes. Negative values are reserved for fatal or internal errors.
2. Error codes are defined in `include/errcodes.h` and must be documented. Currently implemented codes:

```text
0  -> OK
1  -> ERR_INVALID_ARGUMENT
2  -> ERR_BUFFER_TOO_SMALL
3  -> ERR_COMPUTATION_FAILED
4  -> ERR_TIMEOUT
5  -> ERR_NOT_IMPLEMENTED
```

3. Fortran routines must not `stop` or `exit` the process. They must return a status code and optionally write a textual diagnostic into a caller-provided `diag` buffer.
4. The C runtime will convert status codes into actions: retry, checkpoint, abort, or mark task failed according to scheduler policy.

---

## 6. Logging and tracing

1. The C runtime provides a 64 bit `trace_id` in task metadata. This value must be forwarded by C when calling Fortran so logs produced on both sides can be correlated.
2. Fortran may call a simple logging stub function provided by C for structured logs. The stub is optional. If used, it must be provided as a function pointer in the task descriptor.
3. Log levels and format are controlled by the runtime. Fortran should not attempt to open or manage log files.

---

## 7. Task structure and lifecycle

Define a canonical C task structure in `include/task.h` which all coordinators and workers must use.

```c
// include/task.h
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t task_id;        // unique across run
    uint32_t model_id;       // model selector known to runtime
    const void *input;       // pointer to input buffer owned by C
    size_t input_size;       // bytes
    void *output;            // pointer to output buffer owned by C
    size_t output_size;      // bytes available in output
    uint64_t trace_id;       // correlation id for logs/traces
    uint32_t api_version;    // version number of call contract
    uint32_t flags;          // bitflags for optional behaviour
    int32_t timeout_secs;    // <= 0 means no timeout
} task_t;
```

Lifecycle:

1. Coordinator creates and fills `task_t`.
2. Worker runtime validates buffers and `api_version`.
3. Worker calls Fortran entrypoint passing pointers and sizes.
4. Fortran fills output buffer and returns status.
5. Worker interprets status and acts accordingly.

---

## 8. Canonical C -> Fortran call signature

All Fortran entrypoints must follow this canonical C-callable signature. This makes it simple to generate headers and stubs for each task.

```c
// include/fortran_api.h
#include <stdint.h>
#include <stddef.h>

// returns int32 status; 0 == OK
int32_t fortran_model_run_v1(
    const void *input_buf, size_t input_size,
    void *output_buf, size_t output_size,
    const void *meta_buf, size_t meta_size,
    uint64_t trace_id,
    int32_t *out_status_code /* optional extra code */
);
```

Notes:

* `input_buf` and `output_buf` point to contiguous memory. The element type is part of the model contract. Typically this is `double` for numerical data.
* `meta_buf` is optional opaque metadata; if unused, pass NULL/0.
* `out_status_code` may be NULL.
* The exported Fortran symbol must be `fortran_model_run_v1` using `bind(C,name="fortran_model_run_v1")`.

### Example Fortran binding (recommended pattern)

```fortran
! fortran/interface/c_api.f90
module c_api
    use iso_c_binding
    implicit none
contains
    subroutine fortran_model_run_v1(input, input_bytes, output, output_bytes, meta, meta_bytes, trace_id, out_status) bind(C, name="fortran_model_run_v1")
        type(c_ptr), value :: input, output, meta
        integer(c_size_t), value :: input_bytes, output_bytes, meta_bytes
        integer(c_int64_t), value :: trace_id
        integer(c_int32_t), intent(out) :: out_status
        ! Implementation must map c_ptr to Fortran arrays using c_f_pointer
    end subroutine fortran_model_run_v1
end module c_api
```

Implementation must call `c_f_pointer` to map the buffers to Fortran arrays and must not allocate output buffers. If more structured parameters are needed, pack them into `meta_buf`.

---

## 9. Metadata and schema

1. `meta_buf` uses a tiny self-describing binary format. The recommended format is a sequence of TLV records (type:uint16, length:uint32, value:bytes).
2. Reserve type codes for common fields: model hyperparameters, random seed, numeric kinds, and array shape descriptors.
3. Keep the TLV parser simple and defensive.

---

## 10. Message envelope for transport

All distributed messages should use a single small envelope header. This ensures transports are interchangeable.

```c
struct message_header {
    uint32_t magic;      // e.g. 0xDA7A1E00
    uint16_t version;    // envelope version
    uint16_t msg_type;   // e.g. TASK_SUBMIT, TASK_RESULT
    uint64_t task_id;
    uint32_t payload_len; // payload only, excluding header
};
```

Payloads are opaque blobs; their interpretation depends on `msg_type`.

---

## 11. Checkpointing contract

1. Checkpoints are byte blobs produced by C-only code. Fortran does not write checkpoints directly.
2. If a Fortran model needs to produce checkpointable state, it must export a `serialize_state` entrypoint which writes into a buffer provided by C. The canonical signature follows the same pattern as `fortran_model_run_v1`.
3. Checkpoint format must be versioned and documented.

---

## 12. Example minimal interaction (C worker pseudocode)

```c
// worker/executor.c
#include "include/fortran_api.h"

int execute_task(task_t *task) {
    // validate sizes
    if (!task->input || task->input_size == 0) return ERR_INVALID_ARGUMENT;

    // serialise access if Fortran not thread safe
    mutex_lock(&fortran_call_lock);

    int32_t rc = fortran_model_run_v1(task->input, task->input_size,
                                    task->output, task->output_size,
                                    NULL, 0,
                                    task->trace_id,
                                    NULL);
    mutex_unlock(&fortran_call_lock);

return rc;
}
```

---

## 13. Unit testing and mocks

1. Provide a C mock implementation of the Fortran API for unit tests. Place mocks under `tests/mocks/fortran_api_mock.c` and export the same symbol names. This lets C-only unit tests run without the Fortran library.
2. Provide a Fortran mock that mirrors the C side behaviour so Fortran unit tests can run without C runtime.
3. Integration tests must verify the canonical signatures and the TLV meta parsing.

---

## 14. Common pitfalls and checks for reviewers

1. Ensure `bind(C)` names match the header exactly.
2. Ensure `c_f_pointer` mappings respect `input_size` in bytes and element type.
3. Do not allocate or free `output` buffers inside Fortran.
4. Check API version field before assuming behaviour.
5. Keep diagnostic strings within caller-provided buffer sizes.

---

## 15. Appendix A: Recommended small-type enums

```c
// include/result.h
typedef enum {
    STATUS_OK = 0,
    STATUS_INVALID_ARGUMENT = 1,
    STATUS_BUFFER_OVERFLOW = 2,
    STATUS_COMPUTATION_ERROR = 3,
    STATUS_TIMEOUT = 4,
    STATUS_NOT_IMPLEMENTED = 5,
} status_t;
```

---

## 16. Appendix B: Minimal TLV example

Binary layout example for one TLV record:

```
uint16_t type;      // e.g. 0x0001 for seed
uint32_t length;    // length of value in bytes
uint8_t  value[];   // raw bytes
```

Multiple records are concatenated. Parsers must skip unknown types gracefully.

---

## 17. Next steps for maintainers

1. Add the canonical headers shown here into `include/` and commit them so each developer copies the same API.
2. Export a small set of unit test mocks into `tests/mocks/` for local testing.
3. Add a short `interface_contracts.md` into `docs/` summarising these rules for code reviewers.

---

Follow these contracts exactly to ensure independent implementations interoperate. If you need an additional concrete example for a specific model or transport, add a short request in the issue tracker and tag `interface-contracts`.
