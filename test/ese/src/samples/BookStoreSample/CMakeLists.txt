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

# BookStoreSample.c is the upstream Windows source, kept verbatim.
# BookStoreSampleLinux.c is the POSIX-path / no-windows.h variant we use
# on Linux. The output binary is named BookStoreSample either way so
# downstream workflows are platform-agnostic.
if (WIN32)
    add_executable(BookStoreSample BookStoreSample.c)
else ()
    add_executable(BookStoreSample BookStoreSampleLinux.c)
    # windows-shim's specstrings.h gets pulled in via jet.h — that header
    # is C++-only territory once SAL macros expand, so compile as C++.
    set_source_files_properties(BookStoreSampleLinux.c PROPERTIES LANGUAGE CXX)
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