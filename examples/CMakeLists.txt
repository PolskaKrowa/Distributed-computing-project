# examples/CMakeLists.txt
# Builds example programs demonstrating the system

# Only build Fortran examples if Fortran compiler is available
if(CMAKE_Fortran_COMPILER_LOADED)
    # Simple damped oscillator example
    add_executable(example_damped_oscil example_damped_oscil.f90)
    target_link_libraries(example_damped_oscil PRIVATE
        fortran_core
        ${BLAS_LIBRARIES}
        ${LAPACK_LIBRARIES}
    )

    set_target_properties(example_damped_oscil PROPERTIES
        Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules
    )

    # Install examples to bin directory
    install(TARGETS example_damped_oscil DESTINATION bin)
    message(STATUS "Fortran example programs configured")
else()
    message(STATUS "Fortran compiler not available. Fortran examples will be skipped.")
endif()

# Subdirectories for example categories
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/minimal_cluster)
    add_subdirectory(minimal_cluster)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/parameter_sweep)
    add_subdirectory(parameter_sweep)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/volunteer_computing)
    add_subdirectory(volunteer_computing)
endif()

message(STATUS "Example programs configured")
