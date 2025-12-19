# Distributed Computing Project

This project is a distributed computing platform designed to support **amateur and independent research** in physics, mathematics, and related scientific fields.

It prioritises:
- Numerical correctness
- Reproducibility
- Performance on commodity hardware
- Clear separation between science code and systems code

The codebase is written in **Fortran** and **C**, using each language where it is most appropriate.

---

## Project goals

- Provide a reliable framework for running numerical experiments at scale
- Make it easy for non-professional researchers to contribute scientific models
- Support both small local clusters and loosely connected volunteer machines
- Remain portable, inspectable, and long-lived

This is not intended to be a general-purpose cloud framework. It is explicitly science-first.

---

## Language split

### Fortran
Used for:
- Numerical kernels
- Mathematical algorithms
- Physics and maths models
- Scientific data output

Fortran code should be deterministic, side-effect minimal, and free of networking or OS logic.

### C
Used for:
- Distributed scheduling
- Networking and messaging
- Worker orchestration
- Resource management
- Metadata handling

C code should not implement numerical algorithms beyond trivial bookkeeping.

---

## Repository structure

```

.
├── src/        # C runtime, scheduler, networking
├── fortran/    # Fortran numerical kernels and models
├── include/    # Public C headers and stable ABI
├── tests/      # Unit and integration tests
├── examples/   # Example experiments and configurations
├── tools/      # Command-line utilities and diagnostics
├── docs/       # Design and contribution documentation
├── cmake/      # CMake helper modules
└── data/       # Sample inputs and reference outputs

```

The only allowed boundary between languages is via a small, explicit C ABI.

---

## Building

The project uses **CMake** and requires:
- A C compiler
- A Fortran compiler
- BLAS and LAPACK
- MPI (optional, depending on configuration)

Basic build:

```

mkdir build
cd build
cmake ..
make

```

More detailed build instructions are in `docs/`.

---

## Contributing

Contributions are welcome, especially:
- New numerical kernels
- Reference physics or mathematics models
- Improvements to documentation
- Tests and validation cases

Please read:
- `docs/fortran_guidelines.md` before contributing Fortran code
- `docs/c_guidelines.md` before contributing C code

Scientific contributions should include references and validation where possible.

---

## Design principles

- Fortran does maths, C does systems
- Determinism over cleverness
- Explicit interfaces over implicit behaviour
- Correctness before performance
- Readability over abstraction

---

## Licence

See the `LICENSE` file for details.