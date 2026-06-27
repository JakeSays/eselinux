// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// HANDLE infrastructure: lifecycle (Close/Duplicate) and handle flags. The
// kernel objects themselves (construction, destruction, wait) live in
// winapi_kobject.cxx; the per-kind primitive bodies live in winapi_sync.cxx
// and winapi_thread.cxx.
//
// CurrentProcess / CurrentThread sentinels (returned by GetCurrentProcess
// and GetCurrentThread) are -1 / -2 — those bypass the KObject path
// entirely; CloseHandle on them is a no-op.

#include "osstd.hxx"
#include "winapi_kobject.hxx"

using osposix::HandleToK;
using osposix::KObject;

namespace
{
constexpr intptr_t c_currentProcessSentinel = -1;
constexpr intptr_t c_currentThreadSentinel = -2;

inline bool IsPseudoHandle(HANDLE h)
{
    const intptr_t v = reinterpret_cast<intptr_t>(h);
    return v == c_currentProcessSentinel || v == c_currentThreadSentinel;
}
}

extern "C"
{
BOOL CloseHandle(HANDLE hObject)
{
    if (!hObject || IsPseudoHandle(hObject))
    {
        return TRUE;
    }
    KObject* const k = HandleToK(hObject);
    if (k->Dereference())
    {
        delete k;
    }
    return TRUE;
}

BOOL DuplicateHandle(HANDLE /*hSourceProcessHandle*/, HANDLE hSourceHandle,
    HANDLE /*hTargetProcessHandle*/, LPHANDLE lpTargetHandle,
    DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, DWORD dwOptions)
{
    if (!lpTargetHandle)
    {
        return FALSE;
    }

    if (IsPseudoHandle(hSourceHandle))
    {
        // Win32 contract: duplicating GetCurrentThread() yields a real
        // waitable handle to that thread. v1: just hand the sentinel
        // back; engine code uses it only to keep handle protocols flowing.
        *lpTargetHandle = hSourceHandle;
        return TRUE;
    }

    KObject* const k = HandleToK(hSourceHandle);
    k->Reference();
    *lpTargetHandle = hSourceHandle;

    if (dwOptions & DUPLICATE_CLOSE_SOURCE)
    {
        CloseHandle(hSourceHandle);
    }
    return TRUE;
}

BOOL GetHandleInformation(HANDLE /*hObject*/, LPDWORD lpdwFlags)
{
    if (lpdwFlags)
        *lpdwFlags = 0;
    return TRUE;
}

BOOL SetHandleInformation(HANDLE /*hObject*/, DWORD /*dwMask*/, DWORD /*dwFlags*/)
{
    // Linux fd-inheritance / protect-from-close is filesystem-level
    // (FD_CLOEXEC). Engine sets HANDLE_FLAG_PROTECT_FROM_CLOSE on a few
    // long-lived handles purely as belt-and-suspenders against double
    // close — accept and ignore.
    return TRUE;
}
} // extern "C"
