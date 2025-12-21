# Distributed Computing Project - MPI Finder
# Provides consistent MPI discovery and configuration across platforms

if(NOT MPI_FOUND)
    message(STATUS "Searching for MPI...")
    
    # Try to find MPI C and Fortran libraries
    find_package(MPI QUIET COMPONENTS C Fortran)
    
    if(NOT MPI_FOUND)
        # Manual detection as fallback
        find_program(MPICC_PROGRAM mpicc)
        find_program(MPIFORT_PROGRAM mpifort)
        
        if(MPICC_PROGRAM)
            message(STATUS "Found mpicc: ${MPICC_PROGRAM}")
            set(MPI_FOUND TRUE)
        endif()
        
        if(MPIFORT_PROGRAM)
            message(STATUS "Found mpifort: ${MPIFORT_PROGRAM}")
            set(MPI_FOUND TRUE)
        endif()
    endif()
endif()

if(MPI_FOUND)
    message(STATUS "MPI found and enabled")
else()
    message(WARNING "MPI not found. MPI-based distributed features will be unavailable.")
endif()
