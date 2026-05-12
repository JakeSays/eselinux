# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
# Minimal CMakeLists for vendored zstd 1.5.7.  Only the core compressor /
# decompressor surface — no dictionary builder, no legacy-format support,
# no CLI tool.  The ESE Linux port consumes this from
# dev/ese/src/os/posix/winapi_compression.cxx (the RtlCompressBuffer /
# RtlDecompressBufferEx shim that backs the blockcache journal).

set(ZSTD_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

file(GLOB ZSTD_COMMON_SOURCES   ${ZSTD_LIB_DIR}/common/*.c)
file(GLOB ZSTD_COMPRESS_SOURCES ${ZSTD_LIB_DIR}/compress/*.c)
file(GLOB ZSTD_DECOMPRESS_SOURCES
    ${ZSTD_LIB_DIR}/decompress/*.c
    ${ZSTD_LIB_DIR}/decompress/*.S)

add_library(zstd STATIC
    ${ZSTD_COMMON_SOURCES}
    ${ZSTD_COMPRESS_SOURCES}
    ${ZSTD_DECOMPRESS_SOURCES})

#  Static lib gets linked into a PIC shared library (libese.so), so it
#  must itself be PIC.  Matches the engine-wide -fPIC at the root.
set_target_properties(zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(zstd
    PUBLIC  ${ZSTD_LIB_DIR}
    PRIVATE ${ZSTD_LIB_DIR}/common)

#  zstd internals are C, not C++ — turn off the engine's broad C++-isms
#  that the root CMakeLists.txt installs.  These flags are clang-isms
#  ($<COMPILE_LANGUAGE:CXX>-gated at the root) so they don't actually
#  reach the C files, but be explicit: zstd's autoconf assumes a plain
#  C compiler with no MSVC-flavour extensions.
target_compile_definitions(zstd PRIVATE
    ZSTD_DISABLE_ASM=0
    ZSTD_LEGACY_SUPPORT=0
    ZSTD_MULTITHREAD=0)

#  zstd uses things like `__builtin_expect` and `__attribute__((unused))`
#  in its inner loops; quiet the noisier warnings rather than patching
#  upstream source.
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(zstd PRIVATE
        -Wno-unused-function
        -Wno-deprecated-declarations)
endif()
