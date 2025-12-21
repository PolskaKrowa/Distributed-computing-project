# FindZMQ.cmake
# Try to find the ZMQ (ZeroMQ) library
#
# Once done this will define:
#  ZMQ_FOUND        - system has ZMQ
#  ZMQ_INCLUDE_DIRS - the ZMQ include directory
#  ZMQ_LIBRARIES    - the libraries needed to use ZMQ

find_path(ZMQ_INCLUDE_DIR
    NAMES zmq.h
    PATHS /usr/include /usr/local/include /opt/local/include
)

find_library(ZMQ_LIBRARY
    NAMES zmq
    PATHS /usr/lib /usr/local/lib /opt/local/lib /usr/lib/x86_64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZMQ
    REQUIRED_VARS ZMQ_LIBRARY ZMQ_INCLUDE_DIR
)

if(ZMQ_FOUND)
    set(ZMQ_LIBRARIES ${ZMQ_LIBRARY})
    set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIR})
    mark_as_advanced(ZMQ_INCLUDE_DIR ZMQ_LIBRARY)
endif()
