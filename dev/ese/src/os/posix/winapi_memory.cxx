// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Local heap (LocalAlloc / LocalFree) and stack walking on POSIX. The
// engine uses LocalAlloc only for small short-lived buffers — back it
// with malloc/calloc.  Stack walking goes through LLVM libunwind's
// low-level cursor API (already statically linked end-to-end), which
// works on both glibc and musl — unlike <execinfo.h>'s `backtrace()`,
// which is a glibc-only extension that Alpine/musl doesn't ship.

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include "osstd.hxx"

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

    // libunwind walks the local stack frame-by-frame from this point
    // outward.  Step past `FramesToSkip + 1` frames first (the +1 is
    // this function's own frame), then emit up to FramesToCapture
    // instruction pointers into BackTrace.
    unw_context_t context;
    if (unw_getcontext(&context) != 0)
    {
        if (BackTraceHash)
            *BackTraceHash = 0;
        return 0;
    }
    unw_cursor_t cursor;
    if (unw_init_local(&cursor, &context) != 0)
    {
        if (BackTraceHash)
            *BackTraceHash = 0;
        return 0;
    }

    // Skip this function's frame plus the caller's requested skip count.
    for (DWORD i = 0; i < FramesToSkip + 1; ++i)
    {
        if (unw_step(&cursor) <= 0)
        {
            if (BackTraceHash)
                *BackTraceHash = 0;
            return 0;
        }
    }

    DWORD emit = 0;
    while (emit < FramesToCapture)
    {
        unw_word_t ip = 0;
        if (unw_get_reg(&cursor, UNW_REG_IP, &ip) != 0)
        {
            break;
        }
        BackTrace[emit++] = reinterpret_cast<void*>(static_cast<uintptr_t>(ip));
        if (unw_step(&cursor) <= 0)
        {
            break;
        }
    }
    if (emit == 0)
    {
        if (BackTraceHash)
            *BackTraceHash = 0;
        return 0;
    }

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
