# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_executable(ERRVALIDATOR
    util.cxx
    errvalidator.cxx
)

target_compile_definitions(ERRVALIDATOR PRIVATE
    _MBCS
    BUILD_ENV_IS_EX
    ESE_FLAVOUR_IS_ESE
)

target_include_directories(ERRVALIDATOR PRIVATE
    ${ESE_DEV}/published/inc/
    ${INC_OUTPUT_DIRECTORY}
    ${ESE_TEST}/src/inc
)

target_link_libraries(ERRVALIDATOR PRIVATE
    _errstr
)

if (NOT WIN32)
    # ERRVALIDATOR doesn't go through BstfUnitTest, so plug in osposix
    # directly for the Win32 surface (QueryPerformanceCounter, GetTickCount,
    # _strnicmp). Match SYNCUNIT's allow-multiple-defs treatment.
    target_include_directories(ERRVALIDATOR PRIVATE
        ${CMAKE_SOURCE_DIR}/windows-shim
    )
    target_link_libraries(ERRVALIDATOR PRIVATE osposix sync checksum checksum_avx oslite_posix)
    target_link_options(ERRVALIDATOR PRIVATE -Wl,-z,muldefs)
endif ()