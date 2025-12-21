# tests/fortran/CMakeLists.txt
# Fortran unit tests

# Check if Fortran compiler is available
if(NOT CMAKE_Fortran_COMPILER_LOADED)
    message(STATUS "Fortran compiler not available. Fortran tests will be skipped.")
    return()
endif()

# Test for core Fortran modules
add_executable(fortran_core_tests test_core.f90)
target_link_libraries(fortran_core_tests PRIVATE
    fortran_core
    ${BLAS_LIBRARIES}
    ${LAPACK_LIBRARIES}
)

# Set Fortran module directory
set_target_properties(fortran_core_tests PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules
)

# Add to test suite
add_test(NAME fortran_core_tests COMMAND fortran_core_tests)

message(STATUS "Fortran tests configured")
