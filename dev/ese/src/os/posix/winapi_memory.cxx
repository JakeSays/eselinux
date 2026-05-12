// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Local heap (LocalAlloc / LocalFree) and stack walking on POSIX. The
// engine uses LocalAlloc only for small short-lived buffers — back it
// with malloc/calloc. Stack walking goes through glibc backtrace().

#include "osstd.hxx"

#include <execinfo.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
HLOCAL LocalAlloc(UINT uFlags, size_t uBytes)
{
    void* const p = (uFlags & LMEM_ZEROINIT)
                    ? calloc(1, uBytes)
                    : malloc(uBytes);
    return static_cast<HLOCAL>(p);
}

HLOCAL LocalFree(HLOCAL hMem)
{
    free(hMem);
    return nullptr; // Win32 returns NULL on success
}

USHORT RtlCaptureStackBackTrace(DWORD FramesToSkip, DWORD FramesToCapture,
    PVOID* BackTrace, PDWORD BackTraceHash)
{
    if (!BackTrace || FramesToCapture == 0)
    {
        return 0;
    }

    // backtrace() captures starting at the caller; we need to over-capture
    // by FramesToSkip+1 (one extra for this very frame) and then shift.
    constexpr DWORD c_maxFrames = 256;
    void* raw[c_maxFrames];
    const DWORD want = FramesToSkip + FramesToCapture + 1;
    const DWORD cap = (want > c_maxFrames)
                      ? c_maxFrames
                      : want;

    const int got = backtrace(raw, static_cast<int>(cap));
    if (got <= static_cast<int>(FramesToSkip + 1))
    {
        if (BackTraceHash)
            *BackTraceHash = 0;
        return 0;
    }

    const DWORD captured = static_cast<DWORD>(got) - (FramesToSkip + 1);
    const DWORD emit = (captured > FramesToCapture)
                       ? FramesToCapture
                       : captured;
    memcpy(BackTrace, raw + FramesToSkip + 1, emit * sizeof(void*));

    if (BackTraceHash)
    {
        // Cheap rolling hash so distinct stacks bucket apart in dumps.
        DWORD h = 0;
        for (DWORD i = 0; i < emit; ++i)
        {
            h += static_cast<DWORD>(reinterpret_cast<uintptr_t>(BackTrace[i]));
        }
        *BackTraceHash = h;
    }

    return static_cast<USHORT>(emit);
}
} // extern "C"
