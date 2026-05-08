// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 sync primitives over pthread_mutex/cond. Each primitive lives
// inside a KObject (winapi_kobject.hxx) so HANDLE/CloseHandle/Wait*
// flow through the same machinery.
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

#include <errno.h>
#include <time.h>

using osposix::AllocKObject;
using osposix::FreeKObject;
using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;
using osposix::KToHandle;
using osposix::WaitOne;

namespace
{
    void GetMonotonicDeadline( DWORD ms, struct timespec* out )
    {
        clock_gettime( CLOCK_MONOTONIC, out );
        out->tv_sec  += ms / 1000;
        out->tv_nsec += static_cast<long>( ms % 1000 ) * 1000000L;
        if ( out->tv_nsec >= 1000000000L )
        {
            out->tv_sec  += 1;
            out->tv_nsec -= 1000000000L;
        }
    }

    inline bool DeadlinePassed( const struct timespec& deadline )
    {
        struct timespec now;
        clock_gettime( CLOCK_MONOTONIC, &now );
        if ( now.tv_sec  != deadline.tv_sec  ) return now.tv_sec  > deadline.tv_sec;
        return now.tv_nsec > deadline.tv_nsec;
    }

    // Wait while pred() is true (i.e. the wake condition is NOT yet met),
    // honoring an optional monotonic deadline. Returns 0 on satisfaction,
    // ETIMEDOUT on timeout. Caller must hold k->lock.
    template<class Pred>
    int WaitWhile( KObject* k, DWORD ms, Pred pred )
    {
        if ( ms == INFINITE )
        {
            while ( pred() )
            {
                pthread_cond_wait( &k->cond, &k->lock );
            }
            return 0;
        }
        struct timespec deadline;
        GetMonotonicDeadline( ms, &deadline );
        while ( pred() )
        {
            const int rc = pthread_cond_timedwait( &k->cond, &k->lock, &deadline );
            if ( rc == ETIMEDOUT )
            {
                return ETIMEDOUT;
            }
        }
        return 0;
    }
}

namespace osposix
{

DWORD WaitOne( KObject* k, DWORD dwMilliseconds )
{
    if ( !k )
    {
        return WAIT_FAILED;
    }

    pthread_mutex_lock( &k->lock );
    int rc = 0;
    DWORD ret = WAIT_OBJECT_0;

    switch ( k->kind )
    {
    case HandleKind::Event:
        rc = WaitWhile( k, dwMilliseconds, [k]{ return !k->eventSignaled; } );
        if ( rc == ETIMEDOUT ) { ret = WAIT_TIMEOUT; break; }
        if ( !k->eventManualReset )
        {
            k->eventSignaled = false;
        }
        break;

    case HandleKind::Mutex:
    {
        const pthread_t self = pthread_self();
        if ( k->mutexHeld && pthread_equal( k->mutexOwner, self ) )
        {
            ++k->mutexRecursion;
            break;
        }
        rc = WaitWhile( k, dwMilliseconds, [k]{ return k->mutexHeld; } );
        if ( rc == ETIMEDOUT ) { ret = WAIT_TIMEOUT; break; }
        k->mutexHeld      = true;
        k->mutexOwner     = self;
        k->mutexRecursion = 1;
        if ( k->mutexAbandoned )
        {
            k->mutexAbandoned = false;
            ret = WAIT_ABANDONED;
        }
        break;
    }

    case HandleKind::Semaphore:
        rc = WaitWhile( k, dwMilliseconds, [k]{ return k->semCurrent <= 0; } );
        if ( rc == ETIMEDOUT ) { ret = WAIT_TIMEOUT; break; }
        --k->semCurrent;
        break;

    case HandleKind::Thread:
        rc = WaitWhile( k, dwMilliseconds, [k]{ return !k->threadFinished; } );
        if ( rc == ETIMEDOUT ) { ret = WAIT_TIMEOUT; break; }
        break;

    case HandleKind::File:
    case HandleKind::FindFile:
    case HandleKind::FindVolume:
        // Engine doesn't synchronize on file or find handles; fail explicitly
        // rather than blocking on an unrelated condvar.
        ret = WAIT_FAILED;
        break;
    }

    pthread_mutex_unlock( &k->lock );
    return ret;
}

}  // namespace osposix

extern "C" {

// ---- Events --------------------------------------------------------------

HANDLE CreateEventW( LPSECURITY_ATTRIBUTES /*sec*/, BOOL bManualReset,
                     BOOL bInitialState, LPCWSTR /*lpName*/ )
{
    KObject* const k = AllocKObject( HandleKind::Event );
    if ( !k ) return nullptr;
    k->eventManualReset = bManualReset ? true : false;
    k->eventSignaled    = bInitialState ? true : false;
    return KToHandle( k );
}

HANDLE CreateEventA( LPSECURITY_ATTRIBUTES sec, BOOL bManualReset,
                     BOOL bInitialState, LPCSTR /*lpName*/ )
{
    // Named events aren't shared across processes in our v1 implementation,
    // so the name doesn't matter; forward to the wide variant with a NULL
    // name (cf. winapi_handle.cxx — no global name table).
    return CreateEventW( sec, bManualReset, bInitialState, nullptr );
}

HANDLE OpenEventW( DWORD /*access*/, BOOL /*inherit*/, LPCWSTR /*lpName*/ )
{
    // Named events for cross-process sharing aren't supported on Linux v1.
    return nullptr;
}

BOOL SetEvent( HANDLE hEvent )
{
    KObject* const k = HandleToK( hEvent );
    pthread_mutex_lock( &k->lock );
    k->eventSignaled = true;
    if ( k->eventManualReset )
    {
        pthread_cond_broadcast( &k->cond );
    }
    else
    {
        pthread_cond_signal( &k->cond );
    }
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

BOOL ResetEvent( HANDLE hEvent )
{
    KObject* const k = HandleToK( hEvent );
    pthread_mutex_lock( &k->lock );
    k->eventSignaled = false;
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

BOOL PulseEvent( HANDLE hEvent )
{
    // Legacy. Wake all waiters once; do NOT leave the event signaled.
    KObject* const k = HandleToK( hEvent );
    pthread_mutex_lock( &k->lock );
    k->eventSignaled = true;
    pthread_cond_broadcast( &k->cond );
    k->eventSignaled = false;
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

// ---- Mutexes -------------------------------------------------------------

HANDLE CreateMutexW( LPSECURITY_ATTRIBUTES /*sec*/, BOOL bInitialOwner, LPCWSTR /*lpName*/ )
{
    KObject* const k = AllocKObject( HandleKind::Mutex );
    if ( !k ) return nullptr;
    if ( bInitialOwner )
    {
        k->mutexHeld      = true;
        k->mutexOwner     = pthread_self();
        k->mutexRecursion = 1;
    }
    return KToHandle( k );
}

HANDLE OpenMutexW( DWORD /*access*/, BOOL /*inherit*/, LPCWSTR /*lpName*/ )
{
    return nullptr;
}

BOOL ReleaseMutex( HANDLE hMutex )
{
    KObject* const k = HandleToK( hMutex );
    pthread_mutex_lock( &k->lock );
    if ( !k->mutexHeld || !pthread_equal( k->mutexOwner, pthread_self() ) )
    {
        pthread_mutex_unlock( &k->lock );
        return FALSE;
    }
    if ( --k->mutexRecursion == 0 )
    {
        k->mutexHeld = false;
        pthread_cond_signal( &k->cond );
    }
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

// ---- Semaphores ----------------------------------------------------------

HANDLE CreateSemaphoreW( LPSECURITY_ATTRIBUTES /*sec*/, LONG lInitialCount,
                         LONG lMaximumCount, LPCWSTR /*lpName*/ )
{
    KObject* const k = AllocKObject( HandleKind::Semaphore );
    if ( !k ) return nullptr;
    k->semCurrent = lInitialCount;
    k->semMax     = lMaximumCount;
    return KToHandle( k );
}

HANDLE CreateSemaphoreExW( LPSECURITY_ATTRIBUTES sec, LONG lInitialCount,
                           LONG lMaximumCount, LPCWSTR lpName,
                           DWORD /*dwFlags*/, DWORD /*dwDesiredAccess*/ )
{
    // dwFlags is reserved (must be 0); dwDesiredAccess is irrelevant for
    // our access-mask-free POSIX semaphore. Forward to CreateSemaphoreW.
    return CreateSemaphoreW( sec, lInitialCount, lMaximumCount, lpName );
}

BOOL ReleaseSemaphore( HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount )
{
    KObject* const k = HandleToK( hSemaphore );
    pthread_mutex_lock( &k->lock );
    if ( lpPreviousCount )
    {
        *lpPreviousCount = k->semCurrent;
    }
    if ( k->semCurrent + lReleaseCount > k->semMax )
    {
        pthread_mutex_unlock( &k->lock );
        return FALSE;
    }
    k->semCurrent += lReleaseCount;
    for ( LONG i = 0; i < lReleaseCount; ++i )
    {
        pthread_cond_signal( &k->cond );
    }
    pthread_mutex_unlock( &k->lock );
    return TRUE;
}

// ---- Wait family ---------------------------------------------------------

DWORD WaitForSingleObject( HANDLE hHandle, DWORD dwMilliseconds )
{
    return WaitOne( HandleToK( hHandle ), dwMilliseconds );
}

DWORD WaitForSingleObjectEx( HANDLE hHandle, DWORD dwMilliseconds, BOOL /*bAlertable*/ )
{
    return WaitOne( HandleToK( hHandle ), dwMilliseconds );
}

DWORD WaitForMultipleObjects( DWORD nCount, const HANDLE* lpHandles,
                              BOOL bWaitAll, DWORD dwMilliseconds )
{
    return WaitForMultipleObjectsEx( nCount, lpHandles, bWaitAll, dwMilliseconds, FALSE );
}

DWORD WaitForMultipleObjectsEx( DWORD nCount, const HANDLE* lpHandles,
                                BOOL bWaitAll, DWORD dwMilliseconds, BOOL /*bAlertable*/ )
{
    if ( nCount == 0 || !lpHandles )
    {
        return WAIT_FAILED;
    }

    if ( !bWaitAll && nCount == 1 )
    {
        return WaitOne( HandleToK( lpHandles[ 0 ] ), dwMilliseconds );
    }

    // v1 polling implementation. WaitAll semantics require atomic
    // acquisition of all objects, which the engine doesn't actually need
    // (only perfmon.cxx calls this, with bWaitAll=FALSE). The poll path
    // is left in for symmetry — extend with a real condvar-broker if a
    // future caller exercises it on hot paths.
    struct timespec deadline;
    if ( dwMilliseconds != INFINITE )
    {
        GetMonotonicDeadline( dwMilliseconds, &deadline );
    }

    DWORD backoffUs = 100;
    for ( ;; )
    {
        if ( bWaitAll )
        {
            bool allReady = true;
            for ( DWORD i = 0; i < nCount; ++i )
            {
                if ( WaitOne( HandleToK( lpHandles[ i ] ), 0 ) != WAIT_OBJECT_0 )
                {
                    allReady = false;
                    break;
                }
            }
            if ( allReady )
            {
                return WAIT_OBJECT_0;
            }
        }
        else
        {
            for ( DWORD i = 0; i < nCount; ++i )
            {
                const DWORD rc = WaitOne( HandleToK( lpHandles[ i ] ), 0 );
                if ( rc == WAIT_OBJECT_0 || rc == WAIT_ABANDONED )
                {
                    return ( rc == WAIT_ABANDONED ? WAIT_ABANDONED : WAIT_OBJECT_0 ) + i;
                }
            }
        }

        if ( dwMilliseconds != INFINITE && DeadlinePassed( deadline ) )
        {
            return WAIT_TIMEOUT;
        }

        struct timespec sleep;
        sleep.tv_sec  = 0;
        sleep.tv_nsec = static_cast<long>( backoffUs ) * 1000L;
        nanosleep( &sleep, nullptr );
        backoffUs = ( backoffUs < 50000 ) ? backoffUs * 2 : 100000;
    }
}

}  // extern "C"
