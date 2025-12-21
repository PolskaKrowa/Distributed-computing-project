# src/CMakeLists.txt
# Builds the C runtime, scheduling, networking, and orchestration components

# Common library with error handling and logging
add_library(common_lib STATIC
    common/errors.c
    common/errors.h
    common/log.c
    common/log.h
)

target_include_directories(common_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Set C standard for common library
set_target_properties(common_lib PROPERTIES
    C_STANDARD 11
)

# Subdirectories for major components
add_subdirectory(coordinator)
add_subdirectory(worker)
add_subdirectory(runtime)
add_subdirectory(storage)
add_subdirectory(net)

message(STATUS "C runtime components configured")
