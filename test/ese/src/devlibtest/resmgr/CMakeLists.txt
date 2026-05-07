# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

# Linux port scope: only the standalone resmgrunit. The other targets all
# pull in the bf/ ETW-trace test driver tree (rmemulator, bfftldriver,
# resmgrimpl, resmgrreplay), which depends on Win32 VirtualAlloc and ETW
# surface that the windows-shim doesn't cover.
if (WIN32)
    add_subdirectory(resmgremulator)
    add_subdirectory(resmgremulatorunit)
    add_subdirectory(resmgrimpl)
    add_subdirectory(resmgrreplay)
endif ()
add_subdirectory(resmgrunit)