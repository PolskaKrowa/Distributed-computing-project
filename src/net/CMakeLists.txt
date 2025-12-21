# src/net/CMakeLists.txt
# Builds the networking and message transport subsystem

# Find ZMQ (optional)
find_package(ZMQ QUIET)

# Include both transport backends if available
# The dispatcher (transport.c) will route to the appropriate implementation
set(NET_SOURCES
    transport.c
)

# Add backends based on availability
set(HAVE_ZMQ_BACKEND FALSE)
set(HAVE_MPI_BACKEND FALSE)

if(ZMQ_FOUND)
    list(APPEND NET_SOURCES transport_zmq.c)
    set(HAVE_ZMQ_BACKEND TRUE)
    message(STATUS "ZMQ found - will compile ZMQ transport backend")
endif()

if(ENABLE_MPI AND MPI_FOUND)
    list(APPEND NET_SOURCES transport_mpi.c)
    set(HAVE_MPI_BACKEND TRUE)
    message(STATUS "MPI found - will compile MPI transport backend")
endif()

if(NOT HAVE_ZMQ_BACKEND AND NOT HAVE_MPI_BACKEND)
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
if(HAVE_ZMQ_BACKEND)
    target_link_libraries(network_lib PUBLIC ${ZMQ_LIBRARIES})
    target_include_directories(network_lib PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_compile_definitions(network_lib PUBLIC HAVE_ZMQ)
    message(STATUS "Network subsystem configured with ZMQ support")
endif()

if(HAVE_MPI_BACKEND)
    target_link_libraries(network_lib PUBLIC MPI::MPI_C)
    target_include_directories(network_lib PUBLIC ${MPI_C_INCLUDE_DIRS})
    target_compile_definitions(network_lib PUBLIC HAVE_MPI)
    message(STATUS "Network subsystem configured with MPI support")
endif()

set_target_properties(network_lib PROPERTIES
    C_STANDARD 11
)

message(STATUS "Network subsystem configured")
