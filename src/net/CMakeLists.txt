# src/net/CMakeLists.txt
# Builds the networking and message transport subsystem

# Find ZMQ (optional)
find_package(ZMQ QUIET)

# Determine which transport backend to use
# Priority: ZMQ > MPI > error
set(NET_SOURCES
    transport.c
)

if(ZMQ_FOUND)
    list(APPEND NET_SOURCES transport_zmq.c)
    set(HAVE_ZMQ 1)
    message(STATUS "ZMQ found - will use ZMQ transport backend")
elseif(ENABLE_MPI AND MPI_FOUND)
    list(APPEND NET_SOURCES transport_mpi.c)
    set(HAVE_MPI 1)
    message(STATUS "Using MPI transport backend")
else()
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
