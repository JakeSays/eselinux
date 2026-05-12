# Copyright (c) Jake Helfert
# Licensed under the MIT License.

# ese-tests — public-API integration test suite.  Consumes libese.so
# strictly through the published JET API surface (dev/ese/published/inc/
# jet.h + esent.h + the windows-shim type aliases).  Nothing under
# dev/ese/src/{inc,os,ese}/ is on the include path.

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

#  Public API only — windows-shim provides the Win32 type aliases
#  (HANDLE, LONG, WCHAR, …) the same way BookStoreSample consumes them.
#  Anything under dev/ese/src/ is deliberately omitted.
target_include_directories(ese-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/windows-shim
    ${ESE_DEV}/published/inc
    ${INC_OUTPUT_DIRECTORY}            # generated headers (eseex_x.h, jetmsgex.h)
    src
)

target_link_libraries(ese-tests PRIVATE
    ese
    Threads::Threads)
