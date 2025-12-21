# Distributed Computing Project - Compiler Warnings Configuration
# Applies consistent warning flags across different compilers

# This file contains compiler-specific warning configurations to ensure
# consistent code quality and help catch potential bugs early.

function(enable_compiler_warnings target)
    # GCC/Clang C warnings
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wall>
            $<$<COMPILE_LANGUAGE:C>:-Wextra>
            $<$<COMPILE_LANGUAGE:C>:-Wshadow>
            $<$<COMPILE_LANGUAGE:C>:-Wstrict-aliasing>
            $<$<COMPILE_LANGUAGE:C>:-Wwrite-strings>
            $<$<COMPILE_LANGUAGE:C>:-Wconversion>
            $<$<COMPILE_LANGUAGE:C>:-Wsign-conversion>
        )
    endif()
    
    # GCC/Clang Fortran warnings
    if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:Fortran>:-Wall>
            $<$<COMPILE_LANGUAGE:Fortran>:-Wextra>
            $<$<COMPILE_LANGUAGE:Fortran>:-Wimplicit-interface>
            $<$<COMPILE_LANGUAGE:Fortran>:-Wunderflow>
            $<$<COMPILE_LANGUAGE:Fortran>:-Wuninitialized>
        )
    endif()
    
    # MSVC (Windows) warnings
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
endfunction()
