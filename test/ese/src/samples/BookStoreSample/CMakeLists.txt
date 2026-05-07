# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Using Microsoft Visual C++

    add_compile_options(
        /wd4214 # function not inlined - TODO: add it back
        /wd4100 # unreferenced formal parameter
        /wd4214 # nonstandard extension used: bit field of non-integral type
    )
endif()

add_executable(BookStoreSample
    BookStoreSample.c
)

# windows-shim/winnt.h pulls in cc.hxx, which has `extern "C"` blocks —
# compile the sample as C++ on Linux so the shim parses cleanly.
if (NOT WIN32)
    set_source_files_properties(BookStoreSample.c PROPERTIES LANGUAGE CXX)
endif ()

target_compile_definitions(BookStoreSample PRIVATE
    _MBCS
    $<$<BOOL:${WIN32}>:WIN32>
)

if (NOT WIN32)
    target_include_directories(BookStoreSample PRIVATE
        ${CMAKE_SOURCE_DIR}/windows-shim
        ${ESE_DEV}/published/inc        # cc.hxx (basic Win32 type aliases)
    )
endif ()

target_include_directories(BookStoreSample PRIVATE
    ${ESE_DEV}/published/inc            # jet.h
    ${INC_OUTPUT_DIRECTORY}             # generated eseex_x.h
    .                                   # local esent.h shim
)

target_link_libraries(BookStoreSample PRIVATE
    ese

    # Win32 system libs
    $<$<BOOL:${WIN32}>:advapi32>
    $<$<BOOL:${WIN32}>:kernel32>
    $<$<BOOL:${WIN32}>:user32>
)