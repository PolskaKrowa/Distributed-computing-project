# fortran/CMakeLists.txt
# Builds the Fortran numerical kernels and scientific models library

# Check if Fortran compiler is available
if(NOT CMAKE_Fortran_COMPILER_LOADED)
    message(STATUS "Fortran compiler not available. Fortran components will be skipped.")
    message(STATUS "To enable Fortran support, ensure a Fortran compiler is installed and set CMAKE_Fortran_COMPILER.")
    return()
endif()

# Create Fortran modules directory
set(FORTRAN_MOD_DIR "${CMAKE_CURRENT_BINARY_DIR}/modules")
file(MAKE_DIRECTORY ${FORTRAN_MOD_DIR})

# Collect all Fortran source files
set(FORTRAN_CORE_SOURCES
    core/kinds.f90
    core/constants.f90
    core/numeric_utils.f90
    core/rng.f90
)

set(FORTRAN_IO_SOURCES
    # Add io module sources as they become available
)

set(FORTRAN_KERNEL_SOURCES
    kernels/linear_algebra/dense_matrix.f90
    kernels/linear_algebra/sparse_matrix.f90
    kernels/linear_algebra/solve_linear.f90
    kernels/linear_algebra/eigen.f90
    kernels/pde/finite_difference.f90
    kernels/pde/finite_volume.f90
    kernels/pde/spectral.f90
    kernels/ode/backward_euler.f90
    kernels/ode/dormand_prince.f90
    kernels/ode/rk4.f90
    kernels/optimisation/conjugate_newton.f90
    kernels/optimisation/constrained.f90
    kernels/optimisation/gradient_descent.f90
    kernels/optimisation/quasi_newton.f90
    kernels/root_find/newton_rhapson.f90
    kernels/root_find/secant.f90
)

set(FORTRAN_INTERFACE_SOURCES
    # Add interface sources as they become available
)

set(FORTRAN_MODEL_SOURCES
    # Add model sources as they become available
)

# Combine all sources
set(FORTRAN_ALL_SOURCES
    ${FORTRAN_CORE_SOURCES}
    ${FORTRAN_IO_SOURCES}
    ${FORTRAN_KERNEL_SOURCES}
    ${FORTRAN_INTERFACE_SOURCES}
    ${FORTRAN_MODEL_SOURCES}
)

# Build Fortran library
add_library(fortran_core STATIC
    ${FORTRAN_ALL_SOURCES}
)

# Set module output directory
set_target_properties(fortran_core PROPERTIES
    Fortran_MODULE_DIRECTORY ${FORTRAN_MOD_DIR}
)

# Link against BLAS and LAPACK
target_link_libraries(fortran_core PUBLIC
    ${BLAS_LIBRARIES}
    ${LAPACK_LIBRARIES}
)

# Link against MPI if enabled
if(ENABLE_MPI AND MPI_FOUND)
    target_link_libraries(fortran_core PUBLIC MPI::MPI_Fortran)
    target_include_directories(fortran_core PUBLIC ${MPI_Fortran_INCLUDE_DIRS})
    target_compile_definitions(fortran_core PUBLIC HAVE_MPI)
    message(STATUS "Fortran core library configured with MPI support")
endif()

# Include directories
target_include_directories(fortran_core PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${FORTRAN_MOD_DIR}
)

# Set Fortran compiler flags
if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fortran_core PRIVATE
        -ffree-line-length-none
        -fimplicit-none
    )
endif()

message(STATUS "Fortran core library configured")
