cmake_minimum_required(VERSION 3.15)
project(DistributedComputing 
    VERSION 0.1.0
    LANGUAGES C
    DESCRIPTION "A distributed computing platform for scientific research"
)

# Enable Fortran if available
enable_language(Fortran OPTIONAL)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# CMake module search path
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# =============================================================================
# Project Configuration Options
# =============================================================================

option(BUILD_TESTS "Build test suite" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_MPI "Enable MPI support for distributed computing" OFF)
option(ENABLE_HDF5 "Enable HDF5 for advanced data storage" OFF)
option(ENABLE_CUDA "Enable CUDA support (experimental)" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer for debugging" OFF)

# =============================================================================
# Language Standards
# =============================================================================

# C standard - C11 or later
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Fortran - Free form, fixed standard
set(CMAKE_Fortran_PREPROCESS ON)

# =============================================================================
# Compiler Flags
# =============================================================================

# Common flags for all compilers
add_compile_options(-Wall -Wextra -pedantic)

# GCC/Clang C flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C>:-Wshadow>
        $<$<COMPILE_LANGUAGE:C>:-Wstrict-aliasing>
        $<$<COMPILE_LANGUAGE:C>:-Wwrite-strings>
    )
endif()

# GCC/Clang Fortran flags
if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:Fortran>:-Wall>
        $<$<COMPILE_LANGUAGE:Fortran>:-Wextra>
        $<$<COMPILE_LANGUAGE:Fortran>:-Wimplicit-interface>
        $<$<COMPILE_LANGUAGE:Fortran>:-Wunderflow>
    )
endif()

add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Debug flags
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_Fortran_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Release flags
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_Fortran_FLAGS_RELEASE "-O3 -DNDEBUG")

# Address Sanitizer if requested
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# =============================================================================
# Required Dependencies
# =============================================================================

# Find C compiler (required)
if(NOT CMAKE_C_COMPILER)
    message(FATAL_ERROR "No C compiler found. Please set CMAKE_C_COMPILER or ensure a C compiler is installed.")
endif()

# Find Fortran compiler (optional)
if(NOT CMAKE_Fortran_COMPILER)
    message(STATUS "No Fortran compiler found. Fortran components will be unavailable.")
    message(STATUS "To enable Fortran support, ensure a Fortran compiler is installed (e.g., gfortran).")
else()
    message(STATUS "Fortran compiler found: ${CMAKE_Fortran_COMPILER}")
endif()

# BLAS and LAPACK (required for numerical operations)
find_package(BLAS REQUIRED)
find_package(LAPACK REQUIRED)

# =============================================================================
# Optional Dependencies
# =============================================================================

# MPI (optional)
if(ENABLE_MPI)
    find_package(MPI REQUIRED)
    message(STATUS "MPI enabled: ${MPI_C_FOUND}")
endif()

# HDF5 (optional)
if(ENABLE_HDF5)
    find_package(HDF5 REQUIRED COMPONENTS C)
    message(STATUS "HDF5 enabled")
endif()

# CUDA (optional and experimental)
if(ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDA REQUIRED)
    message(STATUS "CUDA enabled")
endif()

# =============================================================================
# Subdirectories
# =============================================================================

# Add main source directories
add_subdirectory(src)

# Only add Fortran subdirectory if Fortran compiler is available
if(CMAKE_Fortran_COMPILER_LOADED)
    add_subdirectory(fortran)
endif()

# Optional: tests and examples
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_DOCS)
    add_subdirectory(docs)
endif()

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "Build Configuration Summary:")
message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "  CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "  CMAKE_Fortran_COMPILER: ${CMAKE_Fortran_COMPILER}")
message(STATUS "  C_FLAGS: ${CMAKE_C_FLAGS}")
message(STATUS "  Fortran_FLAGS: ${CMAKE_Fortran_FLAGS}")
message(STATUS "  BLAS: ${BLAS_LIBRARIES}")
message(STATUS "  LAPACK: ${LAPACK_LIBRARIES}")
if(ENABLE_MPI)
    message(STATUS "  MPI: ENABLED")
endif()
if(ENABLE_HDF5)
    message(STATUS "  HDF5: ENABLED")
endif()
if(ENABLE_CUDA)
    message(STATUS "  CUDA: ENABLED")
endif()
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build docs: ${BUILD_DOCS}")
message(STATUS "")
