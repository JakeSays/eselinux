# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_compile_definitions(_MBCS)

if(WIN32)
    set(SYNC_SOURCES sync.cxx)
else()
    set(SYNC_SOURCES sync.cxx sync_posix.cxx)
endif()

add_library(sync STATIC
    ${SYNC_SOURCES}
)

target_include_directories(sync PRIVATE
    ${INC_OUTPUT_DIRECTORY}/jet
)