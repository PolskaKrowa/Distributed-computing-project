# tests/CMakeLists.txt
# Builds and configures the test suite

# Test discovery function
function(add_test_executable name sources libraries)
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE ${libraries})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Subdirectory organization for different test types
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/c)
    add_subdirectory(c)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fortran)
    add_subdirectory(fortran)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration)
    add_subdirectory(integration)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
    add_subdirectory(mocks)
endif()

message(STATUS "Test suite configured")
