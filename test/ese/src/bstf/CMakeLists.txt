# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_subdirectory(BstfUnitTest)

if (NOT WIN32)
    # oslite_posix carries engine-internal stubs (UtilReportEvent, OSRMTerm,
    # JetErrorToString, ...) that osposix references when libese.so is
    # absent. Mirrors the role of dev/ese/src/os/litent/violated.cxx on
    # Windows. Built once; every devlibtest binary links it.
    add_library(oslite_posix STATIC test_stubs_posix.cxx)
    target_include_directories(oslite_posix PRIVATE
        ${ESE_DEV}/published/inc
        ${INC_OUTPUT_DIRECTORY}
    )
endif ()