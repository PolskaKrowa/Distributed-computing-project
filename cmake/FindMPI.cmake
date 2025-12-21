# Distributed Computing Project - MPI Wrapper
# Provides consistent MPI discovery and configuration across platforms
# This is a thin wrapper that ensures proper MPI detection without infinite recursion

# Remove this module from the path temporarily to avoid recursion
list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# Try to find MPI C and Fortran libraries using CMake's built-in module
find_package(MPI COMPONENTS C Fortran)

# Restore this module path
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}")

if(MPI_FOUND)
    message(STATUS "MPI found successfully")
    message(STATUS "  MPI_C_FOUND: ${MPI_C_FOUND}")
    message(STATUS "  MPI_Fortran_FOUND: ${MPI_Fortran_FOUND}")
else()
    message(WARNING "MPI not found. To install OpenMPI on MSYS2, run: pacman -S mingw-w64-x86_64-openmpi")
    message(WARNING "Or on Linux: sudo apt install libopenmpi-dev")
endif()

mark_as_advanced(MPI_FOUND)
