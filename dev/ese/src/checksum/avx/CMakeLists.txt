# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mavx")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # using Visual Studio C++
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX")
    endif()

    add_compile_definitions(_MBCS)

    add_library(checksum_avx STATIC
        checksum_avx.cxx
    )

    add_dependencies(checksum_avx
        gen_jetmsgex
    )

    target_include_directories(checksum_avx PRIVATE
        ${GEN_OUTPUT_DIRECTORY}/_res/ # For jetmsgex.h
        ${ESE_DEV}/published/inc
        ${ESE_DEV}/published/inc/os
        ${INC_OUTPUT_DIRECTORY}/jet
        ${ESE_DEV}/src/checksum
        ${ESE_DEV}/src/inc/_osu
    )

else ()

    #  Non-x86 (e.g. aarch64): no AVX kernel, but we keep the
    #  `checksum_avx` library name and the `ChecksumNewFormatAVX`
    #  exported symbol — checksum_neon.cxx provides them as a
    #  NEON-accelerated implementation of the same HSIAO ECC
    #  algorithm.  Consumers' target_link_libraries(... checksum_avx)
    #  resolves transparently; the test sites that reference
    #  ChecksumNewFormatAVX (including the `pfnChecksumNewFormat ==
    #  ChecksumNewFormatAVX` selection assertion) work without
    #  per-arch gating.
    add_library(checksum_avx STATIC
        checksum_neon.cxx
    )

    add_dependencies(checksum_avx
        gen_jetmsgex
    )

    target_include_directories(checksum_avx PRIVATE
        ${GEN_OUTPUT_DIRECTORY}/_res/
        ${ESE_DEV}/published/inc
        ${ESE_DEV}/published/inc/os
        ${INC_OUTPUT_DIRECTORY}/jet
        ${ESE_DEV}/src/checksum
        ${ESE_DEV}/src/inc/_osu
    )

endif ()
