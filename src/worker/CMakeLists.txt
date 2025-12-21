# src/worker/CMakeLists.txt
# Builds the worker node orchestration subsystem

set(WORKER_SOURCES
    worker.c
)

add_executable(worker ${WORKER_SOURCES})

target_include_directories(worker PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(worker PUBLIC
    common_lib
    network_lib
    runtime_lib
)

set_target_properties(worker PROPERTIES
    C_STANDARD 11
)

# Install worker to bin directory
install(TARGETS worker DESTINATION bin)

message(STATUS "Worker subsystem configured")
