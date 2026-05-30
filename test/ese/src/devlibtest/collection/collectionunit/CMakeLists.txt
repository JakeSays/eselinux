# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_executable(COLLECTIONUNIT
    countedinvasivelinkedlist.cpp
    main.cxx
    objectpool.cxx
    redblacktree.cxx
    singlelinkedlist.cxx
    queuelist.cxx
    array.cxx
    rrnextprev.cxx
    table.cxx
    approximateindex.cxx
    concurrentmodset.cxx
)

target_compile_definitions(COLLECTIONUNIT PRIVATE
    _MBCS
    BUILD_ENV_IS_EX
    ESE_FLAVOUR_IS_ESE
    _HAS_AUTO_PTR_ETC
)

target_include_directories(COLLECTIONUNIT PRIVATE
    ${ESE_DEV}/published/inc/
    ${INC_OUTPUT_DIRECTORY}
    ${ESE_TEST}/src/inc
)

target_link_libraries(COLLECTIONUNIT PRIVATE
    BstfUnitTest
    checksum
    checksum_avx
    # oslite is the Windows OS-layer-stub static lib; on Linux BstfUnitTest
    # transitively pulls in osposix + the test_stubs_posix.cxx adapter.
    $<$<BOOL:${WIN32}>:oslite>
    sync
)

if (NOT WIN32)
    # Same AssertFail/EnforceFail conflict pattern as SYNCUNIT — see
    # syncunit/CMakeLists.txt.
    target_link_options(COLLECTIONUNIT PRIVATE -Wl,-z,muldefs)
endif ()