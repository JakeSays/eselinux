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

if (WIN32)
    # On Windows the engine is imported from the eselib import stub (esent.def).
    target_link_libraries(eseutil PRIVATE
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
        oswinnt
        ese
        pthread
        dl
    )
else ()
    # On Linux libese.so now exports only the public Jet* API, so eseutil
    # cannot import the engine + OS-layer internals (FOSPreinit, ErrOSInit,
    # COSLayerPreInit, OSTerm, ...) from it. Instead embed the engine
    # statically, mirroring EseLibWithTestsRunner: link the engine objects
    # directly and whole-archive sync + osposix so every OS-layer state
    # global the engine references (g_csError, the kernel semaphore pool,
    # ...) is present. Because eseutil no longer loads libese.so, there is
    # exactly one copy of that state — the copy it owns — so the split-state
    # problem that previously forced libese.so to be the single source is
    # gone.
    target_sources(eseutil PRIVATE $<TARGET_OBJECTS:ese_objects>)
    # _esefile carries its own private copy of ChecksumAndPossiblyFixPage
    # (in xsum.cxx) so the standalone esefile tool can avoid pulling _osu.
    # _osu/checksumu.cxx defines the same routine. Keep _esefile last in the
    # group so the engine's pull of _osu's copy satisfies _esefile's
    # reference first — that way _esefile's duplicate xsum.cxx.o is never
    # pulled and the two definitions don't collide.
    target_link_libraries(eseutil PRIVATE
        -Wl,--start-group
        dht
        _errstr
        _xpress
        _perfctrs
        _log
        _osu
        checksum
        checksum_avx
        _esefile
        -Wl,--end-group
        -Wl,--whole-archive
        sync
        osposix
        -Wl,--no-whole-archive
        pthread
        dl
    )
endif ()

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
