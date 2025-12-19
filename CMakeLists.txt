cmake_minimum_required(VERSION 3.20)

project(
    distributed_science
    LANGUAGES C Fortran
)

# ----------------------------
# Global settings
# ----------------------------

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_Fortran_STANDARD 2008)
set(CMAKE_Fortran_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Sensible warnings
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if (CMAKE_Fortran_COMPILER_ID MATCHES "GNU|Intel|Flang")
    add_compile_options(-Wall)
endif()

# ----------------------------
# Dependencies
# ----------------------------

find_package(LAPACK REQUIRED)

option(ENABLE_MPI "Enable MPI support" ON)
if (ENABLE_MPI)
    find_package(MPI REQUIRED)
endif()

# ----------------------------
# Include paths
# ----------------------------

include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

# ----------------------------
# Fortran science library
# ----------------------------

add_library(science_fortran STATIC
    fortran/core/kinds.f90
    fortran/core/constants.f90
    fortran/core/rng.f90

    fortran/kernels/monte_carlo/estimate_pi.f90
    fortran/models/pi_experiment.f90

    fortran/interface/c_api.f90
)

target_link_libraries(science_fortran
    PRIVATE
        LAPACK::LAPACK
)

# ----------------------------
# C infrastructure library
# ----------------------------

add_library(runtime_c STATIC
    src/common/errors.c
    src/common/log.c
)

# Networking transport library
add_library(net_transport STATIC
    src/net/transport_tcp.c
)

# Common include directories
foreach(lib runtime_c net_transport)
    target_include_directories(${lib}
        PUBLIC ${PROJECT_SOURCE_DIR}/include
    )
endforeach()

# ----------------------------
# Example executable
# ----------------------------

add_executable(run_pi
    tools/experimental/run_pi.c
)

target_link_libraries(run_pi
    PRIVATE
        runtime_c
        science_fortran
)

if (ENABLE_MPI)
    target_link_libraries(run_pi PRIVATE MPI::MPI_C)
endif()

# ----------------------------
# Network transport test tool
# ----------------------------

add_executable(net_test
    tools/diagnostics/net_test.c
)

target_link_libraries(net_test
    PRIVATE
        net_transport
        runtime_c
)

# Place test tools under bin/
set_target_properties(net_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)