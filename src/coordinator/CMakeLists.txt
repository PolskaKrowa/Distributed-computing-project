# src/coordinator/CMakeLists.txt
# Builds the coordinator executable and supporting libraries

set(COORDINATOR_SOURCES
    main.c
    scheduler.c
    scheduler.h
    worker_registry.c
    worker_registry.h
)

add_executable(coordinator ${COORDINATOR_SOURCES})

target_include_directories(coordinator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)

target_link_libraries(coordinator PRIVATE
    common_lib
    network_lib
    runtime_lib
    storage_lib
    Threads::Threads
    m
)

# Link BLAS/LAPACK if needed for numerical operations
if(BLAS_FOUND AND LAPACK_FOUND)
    target_link_libraries(coordinator PRIVATE ${BLAS_LIBRARIES} ${LAPACK_LIBRARIES})
endif()

# Link MPI if available (optional for coordinator)
if(ENABLE_MPI AND MPI_C_FOUND)
    target_link_libraries(coordinator PRIVATE MPI::MPI_C)
endif()

set_target_properties(coordinator PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS coordinator
    RUNTIME DESTINATION bin
    COMPONENT runtime
)

message(STATUS "Coordinator executable configured")