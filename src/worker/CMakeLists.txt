# src/worker/CMakeLists.txt
# Builds the worker subsystem

set(WORKER_SOURCES
    worker.c
    worker.h
)

add_library(worker_lib STATIC ${WORKER_SOURCES})

target_include_directories(worker_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(worker_lib PUBLIC
    common_lib
    network_lib
    runtime_lib
)

set_target_properties(worker_lib PROPERTIES
    C_STANDARD 11
)

# Build worker executable
add_executable(worker_main main_worker.c)

target_link_libraries(worker_main
    worker_lib
    common_lib
    network_lib
    runtime_lib
    pthread
)

set_target_properties(worker_main PROPERTIES
    OUTPUT_NAME "worker"
    C_STANDARD 11
)

install(TARGETS worker_main DESTINATION bin)

message(STATUS "Worker subsystem configured")