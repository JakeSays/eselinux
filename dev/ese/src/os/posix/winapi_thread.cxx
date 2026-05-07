// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 thread management on top of pthreads. Each thread is wrapped in
// a KObject so HANDLE flow / WaitFor* / CloseHandle work uniformly.
// CREATE_SUSPENDED is honored via a barrier inside the thread trampoline
// — the worker waits on threadSuspended before invoking the user proc.
//
// SuspendThread / TerminateThread are not portable on Linux: there is no
// safe equivalent. Engine usage of these is sparse (mostly diagnostics
// and forced shutdown paths); v1 implements SuspendThread as a stub
// failure return, and TerminateThread as pthread_cancel — callers that
// actually rely on these will be ported away from them in Phase 6.

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/resource.h>

using osposix::AllocKObject;
using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;
using osposix::KToHandle;

namespace
{
    void* ThreadTrampoline( void* arg )
    {
        auto* const k = static_cast<KObject*>( arg );

        // CREATE_SUSPENDED: hold here until ResumeThread broadcasts.
        pthread_mutex_lock( &k->lock );
        while ( k->threadSuspended )
        {
            pthread_cond_wait( &k->cond, &k->lock );
        }
        const auto start = k->threadStart;
        const auto param = k->threadParam;
        pthread_mutex_unlock( &k->lock );

        const DWORD rc = start( param );

        pthread_mutex_lock( &k->lock );
        k->threadExitCode = rc;
        k->threadFinished = true;
        pthread_cond_broadcast( &k->cond );
        pthread_mutex_unlock( &k->lock );
        return nullptr;
    }
}

extern "C" {

HANDLE CreateThread( LPSECURITY_ATTRIBUTES /*sec*/, SIZE_T dwStackSize,
                     LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                     DWORD dwCreationFlags, LPDWORD lpThreadId )
{
    KObject* const k = AllocKObject( HandleKind::Thread );
    if ( !k ) return nullptr;

    k->threadStart        = lpStartAddress;
    k->threadParam        = lpParameter;
    k->threadSuspended    = ( dwCreationFlags & CREATE_SUSPENDED ) != 0;
    k->threadExitCode     = STILL_ACTIVE;
    k->threadPriority     = THREAD_PRIORITY_NORMAL;
    k->threadFinished     = false;

    pthread_attr_t attr;
    pthread_attr_init( &attr );
    if ( dwStackSize != 0 )
    {
        pthread_attr_setstacksize( &attr, dwStackSize );
    }

    const int rc = pthread_create( &k->thread, &attr, ThreadTrampoline, k );
    pthread_attr_destroy( &attr );
    if ( rc != 0 )
    {
        free( k );
        return nullptr;
    }

    if ( lpThreadId )
    {
        // Linux pthread_t is opaque; expose a 32-bit hash for diagnostics.
        *lpThreadId = static_cast<DWORD>( reinterpret_cast<uintptr_t>( k ) );
    }
    return KToHandle( k );
}

namespace
{
    // Trampoline for QueueUserWorkItem. Runs the LPTHREAD_START_ROUTINE
    // and discards its return value; pthread_detach takes care of cleanup.
    struct QuwiCtx { LPTHREAD_START_ROUTINE fn; PVOID arg; };
    void* QuwiTrampoline( void* p )
    {
        QuwiCtx* const c = static_cast<QuwiCtx*>( p );
        const LPTHREAD_START_ROUTINE fn = c->fn;
        PVOID const arg = c->arg;
        delete c;
        fn( arg );
        return nullptr;
    }
}

BOOL QueueUserWorkItem( LPTHREAD_START_ROUTINE Function, PVOID Context, ULONG /*Flags*/ )
{
    if ( !Function ) return FALSE;
    QuwiCtx* const ctx = new QuwiCtx{ Function, Context };
    pthread_t tid;
    if ( pthread_create( &tid, nullptr, QuwiTrampoline, ctx ) != 0 )
    {
        delete ctx;
        return FALSE;
    }
    pthread_detach( tid );
    return TRUE;
}

DWORD ResumeThread( HANDLE hThread )
{
    KObject* const k = HandleToK( hThread );
    pthread_mutex_lock( &k->lock );
    const DWORD prior = k->threadSuspended ? 1 : 0;
    if ( k->threadSuspended )
    {
        k->threadSuspended = false;
        pthread_cond_broadcast( &k->cond );
    }
    pthread_mutex_unlock( &k->lock );
    return prior;
}

DWORD SuspendThread( HANDLE /*hThread*/ )
{
    // No portable thread-suspend on Linux. Engine call sites that hit
    // this in v1 will see a -1 return and a failed assertion in DEBUG;
    // we'd rather fail loudly than silently no-op.
    return static_cast<DWORD>( -1 );
}

BOOL TerminateThread( HANDLE hThread, DWORD dwExitCode )
{
    KObject* const k = HandleToK( hThread );
    pthread_cancel( k->thread );
    pthread_mutex_lock( &k->lock );
    k->threadExitCode = dwExitCode;
    k->threadFinished = true;
    pthread_cond_broadcast( &k->cond );
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

BOOL GetExitCodeThread( HANDLE hThread, LPDWORD lpExitCode )
{
    // GetCurrentThread() returns the -2 pseudo-handle (see winapi_handle.cxx).
    // Win32 GetExitCodeThread on that pseudo-handle reports STILL_ACTIVE; we
    // mirror that since we have no KObject for the running thread.
    if ( reinterpret_cast<intptr_t>( hThread ) == -2 ||
         reinterpret_cast<intptr_t>( hThread ) == -1 )
    {
        if ( lpExitCode ) *lpExitCode = STILL_ACTIVE;
        return TRUE;
    }
    KObject* const k = HandleToK( hThread );
    pthread_mutex_lock( &k->lock );
    *lpExitCode = k->threadFinished ? k->threadExitCode : STILL_ACTIVE;
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

BOOL SetThreadPriority( HANDLE hThread, int nPriority )
{
    KObject* const k = HandleToK( hThread );
    pthread_mutex_lock( &k->lock );
    k->threadPriority = nPriority;
    pthread_mutex_unlock( &k->lock );
    // Real priority enforcement requires CAP_SYS_NICE / SCHED_FIFO setup;
    // record the request so GetThreadPriority reflects it.
    return TRUE;
}

int GetThreadPriority( HANDLE hThread )
{
    KObject* const k = HandleToK( hThread );
    pthread_mutex_lock( &k->lock );
    const int p = k->threadPriority;
    pthread_mutex_unlock( &k->lock );
    return p;
}

BOOL SetThreadPriorityBoost( HANDLE hThread, BOOL bDisablePriorityBoost )
{
    KObject* const k = HandleToK( hThread );
    pthread_mutex_lock( &k->lock );
    k->threadPriorityBoostDisabled = bDisablePriorityBoost ? true : false;
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

BOOL GetThreadPriorityBoost( HANDLE hThread, PBOOL pDisablePriorityBoost )
{
    KObject* const k = HandleToK( hThread );
    pthread_mutex_lock( &k->lock );
    *pDisablePriorityBoost = k->threadPriorityBoostDisabled ? TRUE : FALSE;
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

HANDLE OpenThread( DWORD /*access*/, BOOL /*inherit*/, DWORD /*dwThreadId*/ )
{
    // Open-by-tid isn't a primitive on Linux. Engine call sites only use
    // OpenThread( SYNCHRONIZE, ..., id ) to test thread liveness; that
    // pattern needs a thread-table redesign which lands in Phase 6.
    return nullptr;
}

}  // extern "C"
