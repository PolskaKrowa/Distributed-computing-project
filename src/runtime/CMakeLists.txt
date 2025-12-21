# src/runtime/CMakeLists.txt
# Builds the resource management and task queue subsystem

set(RUNTIME_SOURCES
    resource_limits.c
    resource_limits.h
    task_queue.c
    task_queue.h
)

add_library(runtime_lib STATIC ${RUNTIME_SOURCES})

target_include_directories(runtime_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(runtime_lib PUBLIC
    common_lib
)

set_target_properties(runtime_lib PROPERTIES
    C_STANDARD 11
)

message(STATUS "Runtime subsystem configured")
