# Distributed Computing Project - LAPACK Finder (Optional)
# Provides consistent LAPACK discovery across platforms

# LAPACK is optional - system can run without it (using mock/stub implementations)
# This finder attempts to locate LAPACK libraries but doesn't fail if not found

find_library(LAPACK_LIBRARY
    NAMES lapack lapacke openblas
    PATHS /usr/lib /usr/local/lib /opt/local/lib
    NO_DEFAULT_PATH
)

if(LAPACK_LIBRARY)
    set(LAPACK_FOUND TRUE)
    set(LAPACK_LIBRARIES ${LAPACK_LIBRARY})
    message(STATUS "LAPACK found: ${LAPACK_LIBRARY}")
else()
    set(LAPACK_FOUND FALSE)
    set(LAPACK_LIBRARIES "")
    message(STATUS "LAPACK not found (optional - linear algebra operations will use fallback implementations)")
endif()
