# src/coordinator/CMakeLists.txt
# Builds the coordinator subsystem

set(COORDINATOR_SOURCES
    coordinator.c
    coordinator.h
)

add_library(coordinator_lib STATIC ${COORDINATOR_SOURCES})

target_include_directories(coordinator_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(coordinator_lib PUBLIC
    common_lib
    network_lib
    runtime_lib
    storage_lib
)

set_target_properties(coordinator_lib PROPERTIES
    C_STANDARD 11
)

# Build coordinator executable
add_executable(coordinator_main main_coordinator.c)

target_link_libraries(coordinator_main
    coordinator_lib
    common_lib
    network_lib
    runtime_lib
    storage_lib
    pthread
)

set_target_properties(coordinator_main PROPERTIES
    OUTPUT_NAME "coordinator"
    C_STANDARD 11
)

install(TARGETS coordinator_main DESTINATION bin)

message(STATUS "Coordinator subsystem configured")