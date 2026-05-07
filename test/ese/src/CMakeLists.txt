# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(
        /wd4477
        /wd4312
        /wd4459
        /wd4505
        /wd4456
        /wd5205
    )
endif()

add_subdirectory(bstf)
add_subdirectory(devlibtest)
# blue/ and samples/ are Windows-only for the initial Linux port: blue/
# pulls in Exchange/MAPI test plumbing the Linux port doesn't need, and
# samples/ depends on Win32 APIs that aren't shimmed.
if (WIN32)
    add_subdirectory(blue)
    add_subdirectory(samples)
endif()