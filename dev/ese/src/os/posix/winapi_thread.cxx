// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 thread entry points. The thread itself is a ThreadObject
// (winapi_kobject.hxx) which owns the pthread, the CREATE_SUSPENDED barrier,
// and completion bookkeeping; this file only translates the Win32 ABI and
// handles the process-task-level concerns (ioprio, pseudo-handles) that
// aren't per-object state.
//
// SuspendThread / TerminateThread are not portable on Linux: there is no
// safe equivalent. Engine usage of these is sparse (mostly diagnostics
// and forced shutdown paths); v1 implements SuspendThread as a stub
// failure return, and TerminateThread as pthread_cancel — callers that
// actually rely on these will be ported away from them in Phase 6.

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

//  ioprio_set(2): glibc doesn't ship a wrapper.  Hand-roll the constants
//  and call through syscall(SYS_ioprio_set, ...).  Used by
//  SetThreadPriority's THREAD_MODE_BACKGROUND_BEGIN/END handling to
//  reduce kernel IO scheduler priority for background workers
//  (scavenger, async dirty-page flushes).
#ifndef IOPRIO_CLASS_NONE
#define IOPRIO_CLASS_NONE 0
#define IOPRIO_CLASS_RT 1
#define IOPRIO_CLASS_BE 2
#define IOPRIO_CLASS_IDLE 3
#endif

#ifndef IOPRIO_WHO_PROCESS
//  Despite the name, this targets a Linux task (kernel thread), not a
//  process.  `who = 0` means the current task.
#define IOPRIO_WHO_PROCESS 1
#endif

#ifndef IOPRIO_PRIO_VALUE
#define IOPRIO_PRIO_VALUE(cls, data) (((cls) << 13) | (data))
#endif

using osposix::As;
using osposix::KToHandle;
using osposix::ThreadObject;

namespace
{
// Trampoline for QueueUserWorkItem. Runs the LPTHREAD_START_ROUTINE and
// discards its return value; pthread_detach takes care of cleanup.
struct QuwiCtx
{
    LPTHREAD_START_ROUTINE fn;
    PVOID arg;
};

void* QuwiTrampoline(void* p)
{
    QuwiCtx* const c = static_cast<QuwiCtx*>(p);
    const LPTHREAD_START_ROUTINE fn = c->fn;
    PVOID const arg = c->arg;
    delete c;
    fn(arg);
    return nullptr;
}
}

extern "C"
{
HANDLE CreateThread(LPSECURITY_ATTRIBUTES sec, SIZE_T dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
    DWORD dwCreationFlags, LPDWORD lpThreadId)
{
    Unused(sec);
    auto* const t = new ThreadObject(lpStartAddress, lpParameter,
        (dwCreationFlags & CREATE_SUSPENDED) != 0);
    if (!t->Start(dwStackSize))
    {
        delete t;
        return nullptr;
    }

    if (lpThreadId)
    {
        // Linux pthread_t is opaque; expose a 32-bit hash for diagnostics.
        *lpThreadId = static_cast<DWORD>(reinterpret_cast<uintptr_t>(t));
    }
    return KToHandle(t);
}

BOOL QueueUserWorkItem(LPTHREAD_START_ROUTINE Function, PVOID Context, ULONG Flags)
{
    Unused(Flags);
    if (!Function)
    {
        return FALSE;
    }
    QuwiCtx* const ctx = new QuwiCtx{ Function, Context };
    pthread_t tid;
    if (pthread_create(&tid, nullptr, QuwiTrampoline, ctx) != 0)
    {
        delete ctx;
        return FALSE;
    }
    pthread_detach(tid);
    return TRUE;
}

DWORD ResumeThread(HANDLE hThread)
{
    ThreadObject* const t = As<ThreadObject>(hThread);
    if (!t)
    {
        return static_cast<DWORD>(-1);
    }
    return t->Resume();
}

DWORD SuspendThread(HANDLE hThread)
{
    // No portable thread-suspend on Linux. Engine call sites that hit
    // this in v1 will see a -1 return and a failed assertion in DEBUG;
    // we'd rather fail loudly than silently no-op.
    Unused(hThread);
    return static_cast<DWORD>(-1);
}

BOOL TerminateThread(HANDLE hThread, DWORD dwExitCode)
{
    ThreadObject* const t = As<ThreadObject>(hThread);
    if (!t)
    {
        return FALSE;
    }
    t->Terminate(dwExitCode);
    return TRUE;
}

BOOL GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode)
{
    // GetCurrentThread() returns the -2 pseudo-handle (see winapi_handle.cxx).
    // Win32 GetExitCodeThread on that pseudo-handle reports STILL_ACTIVE; we
    // mirror that since we have no KObject for the running thread.
    if (reinterpret_cast<intptr_t>(hThread) == -2 ||
        reinterpret_cast<intptr_t>(hThread) == -1)
    {
        if (lpExitCode)
        {
            *lpExitCode = STILL_ACTIVE;
        }
        return TRUE;
    }
    ThreadObject* const t = As<ThreadObject>(hThread);
    if (!t)
    {
        return FALSE;
    }
    *lpExitCode = t->ExitCode();
    return TRUE;
}

BOOL SetThreadPriority(HANDLE hThread, int nPriority)
{
    //  The two THREAD_MODE_BACKGROUND_* values are the Win32 "drop me
    //  to background priority" / "restore me" pair.  They're always
    //  paired around an IO submission on the calling thread (see
    //  UtilThreadBeginLowIOPriority / EndLowIOPriority in thread.cxx)
    //  so we route them to ioprio_set on the current task instead of
    //  treating them as numeric priority values.
    if (nPriority == THREAD_MODE_BACKGROUND_BEGIN ||
        nPriority == THREAD_MODE_BACKGROUND_END)
    {
        const int cls = nPriority == THREAD_MODE_BACKGROUND_BEGIN
            ? IOPRIO_CLASS_IDLE
            : IOPRIO_CLASS_NONE;
        //  who = 0 → operate on the calling task.  IOPRIO_PRIO_VALUE
        //  packs the (class, data) tuple; data is unused for IDLE /
        //  NONE so we pass 0.  Failures are intentionally swallowed
        //  to match the Win32 behavior (the engine ignores the
        //  return of SetThreadPriority on background-begin/end).
        (void)syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0,
            IOPRIO_PRIO_VALUE(cls, 0));
        return TRUE;
    }

    //  GetCurrentThread() returns the -2 pseudo-handle which has no
    //  backing KObject; for normal numeric priority values on the
    //  current thread we silently no-op (CAP_SYS_NICE is needed for
    //  real enforcement and we don't assume the engine runs with it).
    if (reinterpret_cast<intptr_t>(hThread) == -2 ||
        reinterpret_cast<intptr_t>(hThread) == -1)
    {
        return TRUE;
    }

    auto const t = As<ThreadObject>(hThread);
    if (!t)
    {
        return FALSE;
    }
    //  Real priority enforcement requires CAP_SYS_NICE / SCHED_FIFO setup;
    //  record the request so GetThreadPriority reflects it.
    t->SetPriority(nPriority);
    return TRUE;
}

int GetThreadPriority(HANDLE hThread)
{
    auto const t = As<ThreadObject>(hThread);
    if (!t)
    {
        return THREAD_PRIORITY_NORMAL;
    }
    return t->Priority();
}

BOOL SetThreadPriorityBoost(HANDLE hThread, BOOL bDisablePriorityBoost)
{
    auto const t = As<ThreadObject>(hThread);
    if (!t)
    {
        return FALSE;
    }
    t->SetPriorityBoostDisabled(bDisablePriorityBoost != FALSE);
    return TRUE;
}

BOOL GetThreadPriorityBoost(HANDLE hThread, PBOOL pDisablePriorityBoost)
{
    auto const t = As<ThreadObject>(hThread);
    if (!t)
    {
        return FALSE;
    }
    *pDisablePriorityBoost = t->PriorityBoostDisabled() ? TRUE : FALSE;
    return TRUE;
}

HANDLE OpenThread(DWORD access, BOOL inherit, DWORD dwThreadId)
{
    // Open-by-tid isn't a primitive on Linux. Engine call sites only use
    // OpenThread( SYNCHRONIZE, ..., id ) to test thread liveness; that
    // pattern needs a thread-table redesign which lands in Phase 6.
    Unused(access);
    Unused(inherit);
    Unused(dwThreadId);
    return nullptr;
}
}  // extern "C"
