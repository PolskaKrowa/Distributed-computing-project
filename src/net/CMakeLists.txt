# src/net/CMakeLists.txt
# Builds the networking and message transport subsystem

# Find ZMQ (optional)
find_package(ZMQ QUIET)

# Include both transport backends if available
# The dispatcher (transport.c) will route to the appropriate implementation
set(NET_SOURCES
    transport.c
)

# Add backends based on availability - now both can coexist with vtable pattern
if(ZMQ_FOUND)
    list(APPEND NET_SOURCES transport_zmq.c)
    message(STATUS "ZMQ found - will compile ZMQ transport backend")
endif()

if(ENABLE_MPI AND MPI_FOUND)
    list(APPEND NET_SOURCES transport_mpi.c)
    message(STATUS "MPI found - will compile MPI transport backend")
endif()

if(NOT ZMQ_FOUND AND NOT (ENABLE_MPI AND MPI_FOUND))
    message(FATAL_ERROR "No transport backend available. Install ZMQ or enable MPI.")
endif()

add_library(network_lib STATIC ${NET_SOURCES})

target_include_directories(network_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(network_lib PUBLIC
    common_lib
)

# Link and define backends
if(ZMQ_FOUND)
    target_link_libraries(network_lib PUBLIC ${ZMQ_LIBRARIES})
    target_include_directories(network_lib PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_compile_definitions(network_lib PUBLIC HAVE_ZMQ)
    message(STATUS "Network subsystem configured with ZMQ support")
endif()

if(ENABLE_MPI AND MPI_FOUND)
    target_link_libraries(network_lib PUBLIC MPI::MPI_C)
    target_include_directories(network_lib PUBLIC ${MPI_C_INCLUDE_DIRS})
    target_compile_definitions(network_lib PUBLIC HAVE_MPI)
    message(STATUS "Network subsystem configured with MPI support")
endif()

set_target_properties(network_lib PROPERTIES
    C_STANDARD 11
)

message(STATUS "Network subsystem configured")
