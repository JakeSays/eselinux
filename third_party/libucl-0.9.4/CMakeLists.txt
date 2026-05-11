# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
# Minimal CMakeLists for the vendored libucl 0.9.4.  Only the bits the
# ESE Linux config layer needs: the parser, emitter, hash table, and
# the small set of stdlib HAVE_* macros declared in config.h.  No Lua,
# no OpenSSL, no libfetch/libcurl, no schema utility binary.
#
# Upstream layout was thinned to:
#   include/ucl.h
#   src/{ucl_util,ucl_parser,ucl_emitter,ucl_emitter_streamline,
#        ucl_emitter_utils,ucl_hash,ucl_schema,ucl_msgpack,ucl_sexp}.c
#   src/{ucl_chartable,ucl_hash,ucl_internal,mum,tree,khash,kvec}.h
# plus a hand-written config.h at the project root.

cmake_minimum_required(VERSION 3.20)

add_library(ucl STATIC
    src/ucl_util.c
    src/ucl_parser.c
    src/ucl_emitter.c
    src/ucl_emitter_streamline.c
    src/ucl_emitter_utils.c
    src/ucl_hash.c
    src/ucl_schema.c
    src/ucl_msgpack.c
    src/ucl_sexp.c
)

target_include_directories(ucl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ucl PRIVATE
    HAVE_CONFIG_H=1
)

#  libucl is C99 and isn't warning-clean against clang's full warning
#  set the rest of the engine builds with.  Quiet the noise so we don't
#  fail vendored code on warnings that belong upstream.
target_compile_options(ucl PRIVATE
    -w
)

#  -fPIC so libucl can be linked into libese.so cleanly.
set_target_properties(ucl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
