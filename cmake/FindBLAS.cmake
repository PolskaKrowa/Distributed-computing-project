# Distributed Computing Project - BLAS Finder (Optional)
# Provides consistent BLAS discovery across platforms

# BLAS is optional - system can run without it (using mock/stub implementations)
# This finder attempts to locate BLAS libraries but doesn't fail if not found

find_library(BLAS_LIBRARY
    NAMES blas openblas refblas
    PATHS /usr/lib /usr/local/lib /opt/local/lib
    NO_DEFAULT_PATH
)

if(BLAS_LIBRARY)
    set(BLAS_FOUND TRUE)
    set(BLAS_LIBRARIES ${BLAS_LIBRARY})
    message(STATUS "BLAS found: ${BLAS_LIBRARY}")
else()
    set(BLAS_FOUND FALSE)
    set(BLAS_LIBRARIES "")
    message(STATUS "BLAS not found (optional - numerical operations will use fallback implementations)")
endif()
