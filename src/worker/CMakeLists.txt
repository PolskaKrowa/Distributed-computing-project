# src/worker/CMakeLists.txt
# Builds the worker executable

set(WORKER_SOURCES
    main.c
    executor.c
    executor.h
)

add_executable(worker ${WORKER_SOURCES})

target_include_directories(worker PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)

target_link_libraries(worker PRIVATE
    common_lib
    network_lib
    runtime_lib
    storage_lib
    Threads::Threads
    m
)

# Link BLAS/LAPACK for numerical operations
if(BLAS_FOUND AND LAPACK_FOUND)
    target_link_libraries(worker PRIVATE ${BLAS_LIBRARIES} ${LAPACK_LIBRARIES})
endif()

# Link MPI if available (for cluster head mode)
if(ENABLE_MPI AND MPI_C_FOUND)
    target_link_libraries(worker PRIVATE MPI::MPI_C)
    target_compile_definitions(worker PRIVATE HAVE_MPI)
    message(STATUS "Worker configured with MPI cluster head support")
else()
    message(STATUS "Worker configured in standalone-only mode (no MPI)")
endif()

set_target_properties(worker PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS worker
    RUNTIME DESTINATION bin
    COMPONENT runtime
)

message(STATUS "Worker executable configured")