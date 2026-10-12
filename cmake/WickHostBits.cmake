# One native ABI choice for Lantern, Wick and consumers of the CMake wick target.
# CMake detects sizeof(void*) by compiling for the selected toolchain; the CPU
# running CMake (and its name) does not decide the target width.
set(WICK_HOST_BITS "64" CACHE STRING "Native host pointer width: 64 (default) or 32")
set_property(CACHE WICK_HOST_BITS PROPERTY STRINGS 64 32)
if(NOT WICK_HOST_BITS STREQUAL "64" AND NOT WICK_HOST_BITS STREQUAL "32")
    message(FATAL_ERROR "WICK_HOST_BITS must be exactly 64 or 32 (got '${WICK_HOST_BITS}')")
endif()
if(NOT CMAKE_SIZEOF_VOID_P MATCHES "^(4|8)$")
    message(FATAL_ERROR "Wick requires a compiler-detected 32-bit or 64-bit pointer ABI")
endif()
foreach(_wick_language C CXX)
    if(DEFINED CMAKE_${_wick_language}_SIZEOF_DATA_PTR AND
       NOT CMAKE_${_wick_language}_SIZEOF_DATA_PTR EQUAL CMAKE_SIZEOF_VOID_P)
        message(FATAL_ERROR "Wick requires matching C and C++ pointer widths")
    endif()
endforeach()
math(EXPR _wick_compiler_bits "${CMAKE_SIZEOF_VOID_P} * 8")
if(NOT WICK_HOST_BITS STREQUAL "${_wick_compiler_bits}")
    message(FATAL_ERROR
        "WICK_HOST_BITS=${WICK_HOST_BITS} does not match the compiler's ${_wick_compiler_bits}-bit pointer ABI. Select a matching toolchain and dependencies; this option does not change compiler architecture.")
endif()
message(STATUS "Wick native host: ${WICK_HOST_BITS}-bit compiler pointer ABI")
