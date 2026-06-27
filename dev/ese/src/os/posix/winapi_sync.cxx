// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 sync primitives over the kernel objects in winapi_kobject.cxx. Each
// primitive's blocking semantics live on its EventObject / MutexObject /
// SemaphoreObject; the entry points here just translate the Win32 ABI.
//
// Engine usage notes:
//  - Named events/mutexes (lpName != NULL) are used only for diagnostics
//    process-wide; cross-process sharing was a Windows-era requirement.
//    v1 ignores the name.
//  - PulseEvent is legacy but still referenced; we approximate with
//    Set+Reset under lock — the spurious-wake risk in the Win32 contract
//    means callers already retry.
//  - WaitForMultipleObjects with bWaitAll=FALSE polls the array with an
//    increasing backoff; perfmon.cxx (the only caller) waits on a small
//    array with INFINITE timeout, so latency dominates correctness.

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <time.h>

using osposix::As;
using osposix::DeadlinePassed;
using osposix::EventObject;
using osposix::GetMonotonicDeadline;
using osposix::HandleToK;
using osposix::KObject;
using osposix::KToHandle;
using osposix::MutexObject;
using osposix::SemaphoreObject;

namespace
{
DWORD WaitHandle(HANDLE h, DWORD dwMilliseconds)
{
    KObject* const k = HandleToK(h);
    return k ? k->Wait(dwMilliseconds) : WAIT_FAILED;
}
}

extern "C"
{

// ---- Events --------------------------------------------------------------

HANDLE CreateEventW(LPSECURITY_ATTRIBUTES sec, BOOL bManualReset,
    BOOL bInitialState, LPCWSTR lpName)
{
    Unused(sec);
    Unused(lpName);
    return KToHandle(new EventObject(bManualReset != FALSE, bInitialState != FALSE));
}

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sec, BOOL bManualReset,
    BOOL bInitialState, LPCSTR lpName)
{
    // Named events aren't shared across processes in our v1 implementation,
    // so the name doesn't matter; forward to the wide variant with a NULL
    // name (cf. winapi_handle.cxx — no global name table).
    Unused(lpName);
    return CreateEventW(sec, bManualReset, bInitialState, nullptr);
}

HANDLE OpenEventW(DWORD access, BOOL inherit, LPCWSTR lpName)
{
    // Named events for cross-process sharing aren't supported on Linux v1.
    Unused(access);
    Unused(inherit);
    Unused(lpName);
    return nullptr;
}

BOOL SetEvent(HANDLE hEvent)
{
    EventObject* const e = As<EventObject>(hEvent);
    if (!e)
    {
        return FALSE;
    }
    e->Set();
    return TRUE;
}

BOOL ResetEvent(HANDLE hEvent)
{
    EventObject* const e = As<EventObject>(hEvent);
    if (!e)
    {
        return FALSE;
    }
    e->Reset();
    return TRUE;
}

BOOL PulseEvent(HANDLE hEvent)
{
    EventObject* const e = As<EventObject>(hEvent);
    if (!e)
    {
        return FALSE;
    }
    e->Pulse();
    return TRUE;
}

// ---- Mutexes -------------------------------------------------------------

HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES sec, BOOL bInitialOwner, LPCWSTR lpName)
{
    Unused(sec);
    Unused(lpName);
    return KToHandle(new MutexObject(bInitialOwner != FALSE));
}

HANDLE OpenMutexW(DWORD access, BOOL inherit, LPCWSTR lpName)
{
    Unused(access);
    Unused(inherit);
    Unused(lpName);
    return nullptr;
}

BOOL ReleaseMutex(HANDLE hMutex)
{
    MutexObject* const m = As<MutexObject>(hMutex);
    if (!m)
    {
        return FALSE;
    }
    return m->Release() ? TRUE : FALSE;
}

// ---- Semaphores ----------------------------------------------------------

HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES sec, LONG lInitialCount,
    LONG lMaximumCount, LPCWSTR lpName)
{
    Unused(sec);
    Unused(lpName);
    return KToHandle(new SemaphoreObject(lInitialCount, lMaximumCount));
}

HANDLE CreateSemaphoreExW(LPSECURITY_ATTRIBUTES sec, LONG lInitialCount,
    LONG lMaximumCount, LPCWSTR lpName, DWORD dwFlags, DWORD dwDesiredAccess)
{
    // dwFlags is reserved (must be 0); dwDesiredAccess is irrelevant for
    // our access-mask-free POSIX semaphore. Forward to CreateSemaphoreW.
    Unused(dwFlags);
    Unused(dwDesiredAccess);
    return CreateSemaphoreW(sec, lInitialCount, lMaximumCount, lpName);
}

BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount)
{
    SemaphoreObject* const s = As<SemaphoreObject>(hSemaphore);
    if (!s)
    {
        return FALSE;
    }
    return s->Release(lReleaseCount, lpPreviousCount) ? TRUE : FALSE;
}

// ---- Wait family ---------------------------------------------------------

DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
    return WaitHandle(hHandle, dwMilliseconds);
}

DWORD WaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable)
{
    Unused(bAlertable);
    return WaitHandle(hHandle, dwMilliseconds);
}

DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE* lpHandles,
    BOOL bWaitAll, DWORD dwMilliseconds)
{
    return WaitForMultipleObjectsEx(nCount, lpHandles, bWaitAll, dwMilliseconds, FALSE);
}

DWORD WaitForMultipleObjectsEx(DWORD nCount, const HANDLE* lpHandles,
    BOOL bWaitAll, DWORD dwMilliseconds, BOOL bAlertable)
{
    Unused(bAlertable);
    if (nCount == 0 || !lpHandles)
    {
        return WAIT_FAILED;
    }

    if (!bWaitAll && nCount == 1)
    {
        return WaitHandle(lpHandles[0], dwMilliseconds);
    }

    // v1 polling implementation. WaitAll semantics require atomic
    // acquisition of all objects, which the engine doesn't actually need
    // (only perfmon.cxx calls this, with bWaitAll=FALSE). The poll path
    // is left in for symmetry — extend with a real condvar-broker if a
    // future caller exercises it on hot paths.
    struct timespec deadline;
    if (dwMilliseconds != INFINITE)
    {
        GetMonotonicDeadline(dwMilliseconds, &deadline);
    }

    DWORD backoffUs = 100;
    for (;;)
    {
        if (bWaitAll)
        {
            bool allReady = true;
            for (DWORD i = 0; i < nCount; ++i)
            {
                if (WaitHandle(lpHandles[i], 0) != WAIT_OBJECT_0)
                {
                    allReady = false;
                    break;
                }
            }
            if (allReady)
            {
                return WAIT_OBJECT_0;
            }
        }
        else
        {
            for (DWORD i = 0; i < nCount; ++i)
            {
                const DWORD rc = WaitHandle(lpHandles[i], 0);
                if (rc == WAIT_OBJECT_0 || rc == WAIT_ABANDONED)
                {
                    return (rc == WAIT_ABANDONED ? WAIT_ABANDONED : WAIT_OBJECT_0) + i;
                }
            }
        }

        if (dwMilliseconds != INFINITE && DeadlinePassed(deadline))
        {
            return WAIT_TIMEOUT;
        }

        struct timespec sleep;
        sleep.tv_sec = 0;
        sleep.tv_nsec = static_cast<long>(backoffUs) * 1000L;
        nanosleep(&sleep, nullptr);
        backoffUs = (backoffUs < 50000) ? backoffUs * 2 : 100000;
    }
}
}  // extern "C"
