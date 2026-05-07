// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Critical sections backed by recursive pthread_mutex_t. The Win32
// CRITICAL_SECTION struct from winnt.h is 40 bytes; rather than
// reinterpret-casting an embedded pthread_mutex_t (whose layout varies
// across libcs), we heap-allocate the mutex and stash its pointer in
// the unused DebugInfo field. Engine code treats the struct as opaque,
// so no caller pokes at the other members.
//
// SetCriticalSectionSpinCount / InitializeCriticalSectionAndSpinCount's
// spin count is silently ignored — the recursive mutex already adapts
// (NPTL applies adaptive spinning under PTHREAD_MUTEX_ADAPTIVE_NP).

#include "osstd.hxx"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

namespace
{
    inline pthread_mutex_t* MutexFor( LPCRITICAL_SECTION pcs )
    {
        return static_cast<pthread_mutex_t*>( pcs->DebugInfo );
    }

    pthread_mutex_t* AllocRecursive()
    {
        auto* const m = static_cast<pthread_mutex_t*>( malloc( sizeof( pthread_mutex_t ) ) );
        if ( !m )
        {
            return nullptr;
        }
        pthread_mutexattr_t attr;
        pthread_mutexattr_init( &attr );
        pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
        pthread_mutex_init( m, &attr );
        pthread_mutexattr_destroy( &attr );
        return m;
    }
}

extern "C" {

void InitializeCriticalSection( LPCRITICAL_SECTION pcs )
{
    pcs->DebugInfo      = AllocRecursive();
    pcs->LockCount      = 0;
    pcs->RecursionCount = 0;
    pcs->OwningThread   = nullptr;
    pcs->LockSemaphore  = nullptr;
    pcs->SpinCount      = 0;
}

BOOL InitializeCriticalSectionAndSpinCount( LPCRITICAL_SECTION pcs, DWORD dwSpinCount )
{
    InitializeCriticalSection( pcs );
    pcs->SpinCount = dwSpinCount;
    return ( pcs->DebugInfo != nullptr ) ? TRUE : FALSE;
}

void EnterCriticalSection( LPCRITICAL_SECTION pcs )
{
    pthread_mutex_lock( MutexFor( pcs ) );
}

BOOL TryEnterCriticalSection( LPCRITICAL_SECTION pcs )
{
    return ( pthread_mutex_trylock( MutexFor( pcs ) ) == 0 ) ? TRUE : FALSE;
}

void LeaveCriticalSection( LPCRITICAL_SECTION pcs )
{
    pthread_mutex_unlock( MutexFor( pcs ) );
}

void DeleteCriticalSection( LPCRITICAL_SECTION pcs )
{
    if ( auto* const m = MutexFor( pcs ) )
    {
        pthread_mutex_destroy( m );
        free( m );
        pcs->DebugInfo = nullptr;
    }
}

DWORD SetCriticalSectionSpinCount( LPCRITICAL_SECTION pcs, DWORD dwSpinCount )
{
    const DWORD prior = static_cast<DWORD>( pcs->SpinCount );
    pcs->SpinCount = dwSpinCount;
    return prior;
}

}  // extern "C"
