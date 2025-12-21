# examples/volunteer_computing/CMakeLists.txt
# Volunteer computing example

# Example transport configuration
add_executable(transport_example transport_example.c)
target_link_libraries(transport_example PRIVATE
    common_lib
    network_lib
)

target_include_directories(transport_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link MPI if available
if(ENABLE_MPI AND MPI_FOUND)
    target_link_libraries(transport_example PRIVATE MPI::MPI_C)
endif()

message(STATUS "Volunteer computing example configured")
