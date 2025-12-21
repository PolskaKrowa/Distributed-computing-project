# Distributed Computing Project - MPI Finder
# Provides consistent MPI discovery and configuration across platforms

if(NOT MPI_FOUND)
    message(STATUS "Searching for MPI...")
    
    # Try to find MPI C and Fortran libraries using CMake's built-in module
    find_package(MPI QUIET COMPONENTS C Fortran)
    
    if(NOT MPI_FOUND)
        # Manual detection as fallback for Windows/MSYS2
        find_program(MPICC_PROGRAM mpicc)
        find_program(MPIFORT_PROGRAM mpifort)
        find_program(MPIRUN_PROGRAM mpirun)
        
        # Try to find MPI through environment variables or common install paths
        if(NOT MPICC_PROGRAM)
            # Check MSMPI on Windows
            if(WIN32)
                find_path(MSMPI_BIN mpiexec.exe
                    HINTS "C:/Program Files/Microsoft MPI/Bin"
                          "C:/Program Files (x86)/Microsoft MPI/Bin"
                )
                if(MSMPI_BIN)
                    message(STATUS "Found Microsoft MPI at ${MSMPI_BIN}")
                    set(MPI_FOUND TRUE)
                endif()
            endif()
            
            # Check for OpenMPI in MSYS2 paths
            find_path(MSYS2_OPENMPI_PATH openmpi
                HINTS /mingw64 /mingw32 /usr/local /opt
            )
            if(MSYS2_OPENMPI_PATH)
                message(STATUS "Found OpenMPI in MSYS2: ${MSYS2_OPENMPI_PATH}")
                set(MPI_FOUND TRUE)
            endif()
        endif()
        
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
    message(WARNING "MPI not found. To install OpenMPI on MSYS2, run: pacman -S mingw-w64-x86_64-openmpi")
endif()
