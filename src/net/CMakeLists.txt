# src/net/CMakeLists.txt
# Builds the networking and message transport subsystem

# Find ZMQ (optional)
find_package(ZMQ QUIET)

# Include both transport backends if available
# The dispatcher (transport.c) will route to the appropriate implementation
set(NET_SOURCES
    transport.c
)

# Add ZMQ backend if available
if(ZMQ_FOUND)
    list(APPEND NET_SOURCES transport_zmq.c)
    set(HAVE_ZMQ 1)
    message(STATUS "ZMQ found - ZMQ transport backend will be available")
else()
    message(STATUS "ZMQ not found - ZMQ transport backend will not be available")
endif()

# Add MPI backend if enabled
if(ENABLE_MPI AND MPI_FOUND)
    list(APPEND NET_SOURCES transport_mpi.c)
    set(HAVE_MPI 1)
    message(STATUS "MPI found - MPI transport backend will be available")
elseif(ENABLE_MPI)
    message(STATUS "MPI enabled but not found - MPI transport backend will not be available")
endif()

# Ensure at least one backend is available
if(NOT (ZMQ_FOUND OR (ENABLE_MPI AND MPI_FOUND)))
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

# Link ZMQ if available
if(ZMQ_FOUND)
    target_link_libraries(network_lib PUBLIC ${ZMQ_LIBRARIES})
    target_include_directories(network_lib PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_compile_definitions(network_lib PUBLIC HAVE_ZMQ)
    message(STATUS "Network subsystem configured with ZMQ support")
endif()

# Link MPI if enabled
if(ENABLE_MPI)
    if(MPI_FOUND)
        target_link_libraries(network_lib PUBLIC MPI::MPI_C)
        target_include_directories(network_lib PUBLIC ${MPI_C_INCLUDE_DIRS})
        target_compile_definitions(network_lib PUBLIC HAVE_MPI)
        message(STATUS "Network subsystem configured with MPI support")
    endif()
endif()

set_target_properties(network_lib PROPERTIES
    C_STANDARD 11
)

message(STATUS "Network subsystem configured")
