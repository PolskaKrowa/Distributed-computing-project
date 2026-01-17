# examples/minimal_cluster/CMakeLists.txt
# Minimal cluster example with C runtime integration

# Example runtime configuration
add_executable(runtime_example runtime_example.c)
target_link_libraries(runtime_example PRIVATE
    common_lib
    runtime_lib
)

target_include_directories(runtime_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Example storage configuration
add_executable(storage_example storage_example.c)
target_link_libraries(storage_example PRIVATE
    common_lib
    storage_lib
    m
)

target_include_directories(storage_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

message(STATUS "Minimal cluster example configured")
