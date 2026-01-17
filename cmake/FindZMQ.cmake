# FindZMQ.cmake - Find ZeroMQ library
#
# This module defines:
#  ZMQ_FOUND - System has ZeroMQ
#  ZMQ_INCLUDE_DIRS - ZeroMQ include directories
#  ZMQ_LIBRARIES - Libraries needed to use ZeroMQ
#  ZMQ_VERSION - The version of ZeroMQ found

find_path(ZMQ_INCLUDE_DIR
    NAMES zmq.h
    PATHS
        /usr/include
        /usr/local/include
        /opt/local/include
        ${ZMQ_ROOT}/include
    DOC "ZeroMQ include directory"
)

find_library(ZMQ_LIBRARY
    NAMES zmq libzmq
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/local/lib
        ${ZMQ_ROOT}/lib
    DOC "ZeroMQ library"
)

# Try to find version
if(ZMQ_INCLUDE_DIR AND EXISTS "${ZMQ_INCLUDE_DIR}/zmq.h")
    file(STRINGS "${ZMQ_INCLUDE_DIR}/zmq.h" ZMQ_VERSION_MAJOR_LINE REGEX "^#define[ \t]+ZMQ_VERSION_MAJOR[ \t]+[0-9]+")
    file(STRINGS "${ZMQ_INCLUDE_DIR}/zmq.h" ZMQ_VERSION_MINOR_LINE REGEX "^#define[ \t]+ZMQ_VERSION_MINOR[ \t]+[0-9]+")
    file(STRINGS "${ZMQ_INCLUDE_DIR}/zmq.h" ZMQ_VERSION_PATCH_LINE REGEX "^#define[ \t]+ZMQ_VERSION_PATCH[ \t]+[0-9]+")
    
    string(REGEX REPLACE "^#define[ \t]+ZMQ_VERSION_MAJOR[ \t]+([0-9]+)" "\\1" ZMQ_VERSION_MAJOR "${ZMQ_VERSION_MAJOR_LINE}")
    string(REGEX REPLACE "^#define[ \t]+ZMQ_VERSION_MINOR[ \t]+([0-9]+)" "\\1" ZMQ_VERSION_MINOR "${ZMQ_VERSION_MINOR_LINE}")
    string(REGEX REPLACE "^#define[ \t]+ZMQ_VERSION_PATCH[ \t]+([0-9]+)" "\\1" ZMQ_VERSION_PATCH "${ZMQ_VERSION_PATCH_LINE}")
    
    set(ZMQ_VERSION "${ZMQ_VERSION_MAJOR}.${ZMQ_VERSION_MINOR}.${ZMQ_VERSION_PATCH}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZMQ
    REQUIRED_VARS ZMQ_LIBRARY ZMQ_INCLUDE_DIR
    VERSION_VAR ZMQ_VERSION
)

if(ZMQ_FOUND)
    set(ZMQ_LIBRARIES ${ZMQ_LIBRARY})
    set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIR})
    
    if(NOT TARGET ZMQ::ZMQ)
        add_library(ZMQ::ZMQ UNKNOWN IMPORTED)
        set_target_properties(ZMQ::ZMQ PROPERTIES
            IMPORTED_LOCATION "${ZMQ_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ZMQ_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(ZMQ_INCLUDE_DIR ZMQ_LIBRARY)