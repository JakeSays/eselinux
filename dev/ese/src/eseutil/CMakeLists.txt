# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_executable(eseutil
    eseutil.cxx
    dbspacedump.cxx
)

# ESENT gates off the Exchange-only RESTORE_SERVER_SIMULATION /
# esebcli2 / eseback2 / NativeWatson code paths in eseutil.cxx so the
# binary can build standalone against libese.so. ESESHADOW (volume
# snapshot integration) is dropped for the Linux port.
target_compile_definitions(eseutil PRIVATE
    _MBCS
    ESENT
)

# eseutil has a few MSVC-isms (narrowing case labels, microsoft goto) that
# clang surfaces as warnings/errors. Demote them to warnings so the binary
# builds; the underlying code is benign.
target_compile_options(eseutil PRIVATE
    -Wno-c++11-narrowing
    -ferror-limit=0
)

target_link_libraries(eseutil PRIVATE
    $<$<NOT:$<BOOL:${WIN32}>>:-Wl,--start-group>
    _esefile
    dht
    sync
    _errstr
    _xpress
    _perfctrs
    _log
    _osu
    checksum
    checksum_avx
    $<$<BOOL:${WIN32}>:oswinnt>
    # On Linux, deliberately do NOT link `osposix` here — pulling osposix.a
    # into eseutil gives the binary its own private copy of every static
    # global the OS layer owns (g_dwPageReserveGran, g_csError, the kernel
    # semaphore pool, ...). libese.so also links osposix.a. The two copies
    # diverge at runtime: wmain → FOSPreinit(libese.so) writes one set of
    # globals; FOSRMPreinit(libese.so) reads the same set; but anything in
    # eseutil's own .o files that touched those globals would see zeros.
    # Letting libese.so be the single source (its `-fvisibility=default`
    # exports the OS layer entry points) keeps every TU bound to one set
    # of state.
    $<$<NOT:$<BOOL:${WIN32}>>:-Wl,--end-group>
    # Jet API (JetInit, JetSetSystemParameterW, ...) and the OS layer entry
    # points are exported by libese.so on Linux. On Windows the def-file
    # provides the same surface as part of the eselib import stub, so this
    # dependency is Linux-only.
    $<$<NOT:$<BOOL:${WIN32}>>:ese>
    pthread
    dl
)

target_include_directories(eseutil PRIVATE
    ${GEN_OUTPUT_DIRECTORY}/_res/
    ${INC_OUTPUT_DIRECTORY}
    ${ESE_DEV}/src/inc/_osu/
    ${ESE_DEV}/src/_esefile/
    ./
    ../
)

if(NOT WIN32)
    target_include_directories(eseutil PRIVATE
        ${CMAKE_SOURCE_DIR}/windows-shim
    )
endif()
