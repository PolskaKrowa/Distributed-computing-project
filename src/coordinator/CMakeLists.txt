# src/coordinator/CMakeLists.txt
# Builds the task coordination and scheduling subsystem

set(COORDINATOR_SOURCES
    coordinator.c
)

add_executable(coordinator ${COORDINATOR_SOURCES})

target_include_directories(coordinator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(coordinator PUBLIC
    common_lib
    network_lib
    runtime_lib
)

set_target_properties(coordinator PROPERTIES
    C_STANDARD 11
)

# Install coordinator to bin directory
install(TARGETS coordinator DESTINATION bin)

message(STATUS "Coordinator subsystem configured")
