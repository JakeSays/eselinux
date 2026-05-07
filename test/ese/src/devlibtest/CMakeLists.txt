# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

# bf/ contains the BF ETW/FTL trace drivers. They include <Windows.h>
# (case-sensitive on Linux) and pull in Win32 surface that the windows-shim
# doesn't cover. Not unit tests in the strict sense — the BF unit tests
# proper live elsewhere; revisit when the engine ETW path is ported.
if (WIN32)
    add_subdirectory(bf)
endif ()
add_subdirectory(cclayer)
add_subdirectory(collection)
add_subdirectory(errdata)
add_subdirectory(iterquery)
# oslayer tests exercise Win32 VirtualAlloc / memory-mapping / ETW / etc.
# directly. Porting them needs a sizable additional surface in the
# windows-shim. Gate off for now; revisit after the simpler tests land.
if (WIN32)
    add_subdirectory(oslayer)
endif ()
add_subdirectory(resmgr)
add_subdirectory(stat)
add_subdirectory(sync)