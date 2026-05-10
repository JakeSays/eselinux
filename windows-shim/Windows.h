// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Capital-W Windows.h alias. Some upstream headers (e.g.
// dev/ese/src/ese/oslayer_iomgr_test.cxx) `#include <Windows.h>`
// rather than `<windows.h>`; on case-sensitive filesystems clang would
// otherwise fail to find the lowercase shim. Forwarding here keeps the
// upstream sources untouched.
#include <windows.h>
