# Copyright (c) Jake Helfert
# Licensed under the MIT License.

# ese-tests — public-API integration test suite.  Consumes libese.so
# strictly through the repo-root jetapi.h header, which is pinned at
# JET_VERSION 0x0A01 and depends only on <stdint.h> (+ <uchar.h> in C).
# Nothing under dev/ese/src/, dev/ese/published/, or windows-shim/ is
# on the include path — the test target sees the engine as a black box
# behind libese.so + the standalone JET API surface.

find_package(Threads REQUIRED)

add_executable(ese-tests
    src/Main.cxx

    src/Framework/Check.cxx
    src/Framework/CrashHelper.cxx
    src/Framework/DataGenerator.cxx
    src/Framework/EseDatabase.cxx
    src/Framework/EseInstance.cxx
    src/Framework/EseSession.cxx
    src/Framework/EseTable.cxx
    src/Framework/EseTransaction.cxx
    src/Framework/ScaleProfile.cxx
    src/Framework/Scenario.cxx
    src/Framework/ScenarioRegistry.cxx
    src/Framework/TemporaryDirectory.cxx
    src/Framework/ThreadGroup.cxx

    src/Scenarios/BackupRestoreScenarios.cxx
    src/Scenarios/ColumnTypeScenarios.cxx
    src/Scenarios/ConcurrencyScenarios.cxx
    src/Scenarios/DatabaseScenarios.cxx
    src/Scenarios/DataManipulationScenarios.cxx
    src/Scenarios/ErrorScenarios.cxx
    src/Scenarios/EscrowScenarios.cxx
    src/Scenarios/LimitScenarios.cxx
    src/Scenarios/LongValueScenarios.cxx
    src/Scenarios/MaintenanceScenarios.cxx
    src/Scenarios/MultiValueScenarios.cxx
    src/Scenarios/NavigationScenarios.cxx
    src/Scenarios/PlatformScenarios.cxx
    src/Scenarios/RecoveryScenarios.cxx
    src/Scenarios/ScaleScenarios.cxx
    src/Scenarios/SchemaScenarios.cxx
    src/Scenarios/SessionScenarios.cxx
    src/Scenarios/SnapshotScenarios.cxx
    src/Scenarios/TemporaryTableScenarios.cxx
    src/Scenarios/TransactionScenarios.cxx
)

set_target_properties(ese-tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)

#  Root CMakeLists.txt installs -fno-exceptions / -fno-rtti for the
#  engine.  The integration tests want both — Scenario failures bubble
#  out as exceptions, and we use std::exception_ptr in ThreadGroup.
target_compile_options(ese-tests PRIVATE -fexceptions -frtti)

#  Public API only — jetapi.h is the standalone header and the only
#  ESE-side include path.  No windows-shim, no published/inc, no
#  generated build-tree headers — those are engine-internal.
target_include_directories(ese-tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    src
)

target_link_libraries(ese-tests PRIVATE
    ese
    Threads::Threads)


# ese-config-test — standalone integration test for the libucl-backed
# registry replacement (`dev/ese/src/os/posix/config_posix.cxx`).
#
# Separate executable because the loader is `pthread_once`-cached: once
# the engine reads its merged UCL object in this process, it can't be
# asked to read a different one without re-exec.  Each subtest in the
# binary runs in a forked child so it can plant its own `.ese.conf`
# (the per-binary file the loader looks for next to /proc/self/exe)
# without poisoning the others.

add_executable(ese-config-test config-test/main.cxx)

set_target_properties(ese-config-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)

target_compile_options(ese-config-test PRIVATE -fexceptions -frtti)

target_include_directories(ese-config-test PRIVATE
    ${CMAKE_SOURCE_DIR})

target_link_libraries(ese-config-test PRIVATE ese)
