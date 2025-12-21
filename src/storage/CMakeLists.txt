# src/storage/CMakeLists.txt
# Builds the data storage and metadata management subsystem

# Find dependencies
find_package(HDF5 QUIET COMPONENTS C)
find_package(SQLite3 QUIET)

set(STORAGE_SOURCES
    metadata.c
    metadata.h
)

# Only add HDF5 index if both HDF5 and SQLite3 are available
if(HDF5_FOUND AND SQLite3_FOUND)
    list(APPEND STORAGE_SOURCES
        hdf5_index.c
        hdf5_index.h
    )
    set(HAVE_HDF5 1)
    set(HAVE_SQLITE3 1)
else()
    if(NOT HDF5_FOUND)
        message(STATUS "HDF5 not found - HDF5 index disabled")
    endif()
    if(NOT SQLite3_FOUND)
        message(STATUS "SQLite3 not found - HDF5 index disabled")
    endif()
endif()

add_library(storage_lib STATIC ${STORAGE_SOURCES})

target_include_directories(storage_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(storage_lib PUBLIC
    common_lib
)

# Link HDF5 and SQLite3 if available
if(HDF5_FOUND AND SQLite3_FOUND)
    target_link_libraries(storage_lib PUBLIC ${HDF5_LIBRARIES} ${SQLite3_LIBRARIES})
    target_include_directories(storage_lib PRIVATE ${HDF5_INCLUDE_DIRS} ${SQLite3_INCLUDE_DIRS})
    target_compile_definitions(storage_lib PUBLIC HAVE_HDF5 HAVE_SQLITE3)
    message(STATUS "Storage subsystem configured with HDF5 and SQLite3 support")
endif()

set_target_properties(storage_lib PROPERTIES
    C_STANDARD 11
)

message(STATUS "Storage subsystem configured")

