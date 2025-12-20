# Contributing to the Distributed Computing Project's development

Thank you for your interest in contributing.  
This project is a distributed scientific computing platform written in **Fortran** and **C**, designed to support amateur research in physics and mathematics.

We value correctness, clarity, and reproducibility over cleverness.

---

## Ways to contribute

You can contribute by:
- Adding new **Fortran numerical kernels or models**
- Improving existing scientific implementations
- Fixing bugs or improving performance
- Writing tests or examples
- Improving documentation

If you are unsure where to start, look in `examples/` or `tests/`.

---

## Language responsibilities

### Fortran
Use **Fortran** for:
- Numerical algorithms
- Mathematical models
- Physics simulations
- Scientific data generation

Fortran code lives in:
```

fortran/

```

Rules:
- No networking
- No threading
- No OS-specific code
- No global mutable state
- Deterministic results preferred

All public entry points must be declared using `bind(C)` and placed in:
```

fortran/interface/

```

---

### C
Use **C** for:
- Distributed scheduling
- Networking and messaging
- Task orchestration
- Runtime control and safety

C code lives in:
```

src/

```

Rules:
- No numerical algorithms
- No scientific assumptions
- No Fortran module access outside the interface layer

---

## Cross-language interface

The **only** allowed boundary between C and Fortran is:
```

src/  <-->  fortran/interface/

```

Do not:
- Call Fortran modules directly from C
- Share global variables across languages
- Pass complex pointer graphs across the boundary

Use plain data structures and explicit sizes.

---

## Code style

### General
- Be clear and explicit
- Prefer readability over brevity
- Comment non-obvious decisions

### C
- Use `snake_case`
- One public function per header
- Check all return values
- No hidden memory ownership

### Fortran
- Use `implicit none`
- One module per file
- Explicit interfaces only
- Prefer pure and elemental procedures where possible

---

## Testing

All new code should include tests.

- Fortran tests go in:
```

tests/fortran/

```

- C tests go in:
```

tests/c/

```

- Distributed or end-to-end tests go in:
```

tests/integration/

```

Numerical results should be reproducible within reasonable floating-point tolerances.

---

## Documentation

If you add or change behaviour:
- Update relevant files in `docs/`
- Add an example if possible

Documentation does not need to be verbose, only correct.

---

## Build requirements

The project uses **CMake**.

Before submitting a change, ensure:
- The project builds with a standard C and Fortran compiler
- No warnings are introduced
- Tests pass on at least one platform

---

## Submitting changes

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests
5. Submit a pull request with a clear description

Please explain:
- What problem you are solving
- Why your approach is correct
- Any numerical or scientific assumptions made
- Evidence of fix/feature testing

---

## Code of conduct

Be respectful and constructive.  
Scientific disagreement is welcome; personal attacks are not.

---

Thank you for contributing to the Distributed Computing Project.
