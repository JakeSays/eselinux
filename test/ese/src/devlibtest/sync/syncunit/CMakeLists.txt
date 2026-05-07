# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SYNCUNIT_SOURCES
    main.cxx
    util.cxx
    atomics.cxx
    atomicsmt.cxx
    initonce.cxx
    inittermlock.cxx
    leakdetection.cxx
    meteredsection.cxx
    semaphore.cxx
)
# semaphoreperf.cxx exercises Win32-specific perf APIs (GetProcessTimes,
# raw _InterlockedIncrement64 on LONGLONG) that the windows-shim doesn't
# cover. It's a perf benchmark, not a correctness test, so the Linux port
# skips it for now; revisit once we have a portable timing harness.
if (WIN32)
    list(APPEND SYNCUNIT_SOURCES semaphoreperf.cxx)
endif ()

add_executable(SYNCUNIT ${SYNCUNIT_SOURCES})

target_include_directories(SYNCUNIT PRIVATE
    ${ESE_DEV}/published/inc/
    ${INC_OUTPUT_DIRECTORY}
    ${ESE_TEST}/src/inc
)

# E.g., pthreads
find_package (Threads REQUIRED)

target_link_libraries(SYNCUNIT PRIVATE
    BstfUnitTest
    sync
    # E.g., pthreads
    ${CMAKE_THREAD_LIBS_INIT}
)

if (NOT WIN32)
    # The test provides its own AssertFail/EnforceFail (in util.cxx) that
    # routes asserts to the test runner instead of crashing the process.
    # The OS layer in osposix.a also defines these as part of its standard
    # error path, which the linker pulls in transitively when satisfying
    # other osposix references. Tell lld to take the first definition (the
    # test's), which is correct for a unit-test binary.
    target_link_options(SYNCUNIT PRIVATE -Wl,-z,muldefs)
endif ()