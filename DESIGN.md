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
│   │   │   └── eigen.f90
│   │   │
│   │   ├── monte_carlo/
│   │   │   ├── rng.f90
│   │   │   └── sampling.f90
│   │   │
│   │   └── pde/
│   │       ├── heat_equation.f90
│   │       └── wave_equation.f90
│   │
│   ├── models/
│   │   ├── classical_mechanics/
│   │   │   └── n_body.f90
│   │   │
│   │   ├── quantum/
│   │   │   └── schrodinger.f90
│   │   │
│   │   └── maths/
│   │       └── optimisation.f90
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
- Use modern Fortran (2008 or newer)
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