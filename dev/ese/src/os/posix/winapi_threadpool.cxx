// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 thread-pool API (TP_*). The engine posts background work
// (deferred bookkeeping, async I/O completion) here and waits on the
// returned TP_WORK / TP_TIMER / TP_WAIT handles. Our implementation is
// thread-per-submit: each Submit/Set spawns a pthread that runs the
// callback once, and the TP_* struct carries a count + condvar so
// WaitForThreadpoolXxxCallbacks can block until in-flight callbacks
// drain.
//
// What's intentionally minimal: pool min/max thread counts are recorded
// but ignored (we don't reuse threads between submissions); cleanup
// groups are stubs that the engine treats as opaque cookies; timer
// "period" repetition is not supported (engine only uses one-shot
// timers in the OS sources we've ported so far).
//
// Each Tp* struct is heap-allocated and freed in CloseThreadpoolXxx.
// The engine treats them as opaque pointers, so we do not need to match
// any particular layout.

#include "osstd.hxx"

#include <atomic>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace
{
    struct TpPool
    {
        DWORD threadMin;
        DWORD threadMax;
    };

    struct TpCleanupGroup
    {
        char placeholder;
    };

    // Common header for Work / Timer / Wait — tracks in-flight count so
    // WaitForThreadpoolXxxCallbacks can block until callbacks return.
    struct TpCommon
    {
        pthread_mutex_t lock;
        pthread_cond_t  cond;
        int             inflight;
        bool            cancelled;
    };

    void TpCommonInit( TpCommon* c )
    {
        pthread_mutex_init( &c->lock, nullptr );
        pthread_cond_init( &c->cond, nullptr );
        c->inflight  = 0;
        c->cancelled = false;
    }

    void TpCommonDestroy( TpCommon* c )
    {
        pthread_cond_destroy( &c->cond );
        pthread_mutex_destroy( &c->lock );
    }

    void TpCommonWait( TpCommon* c )
    {
        pthread_mutex_lock( &c->lock );
        while ( c->inflight > 0 ) pthread_cond_wait( &c->cond, &c->lock );
        pthread_mutex_unlock( &c->lock );
    }

    struct TpWork
    {
        TpCommon          common;
        PTP_WORK_CALLBACK callback;
        PVOID             context;
    };

    struct TpTimer
    {
        TpCommon           common;
        PTP_TIMER_CALLBACK callback;
        PVOID              context;
        std::atomic<bool>  armed;
        pthread_t          thread;
        struct timespec    fireAt;
    };

    struct TpWait
    {
        TpCommon          common;
        PTP_WAIT_CALLBACK callback;
        PVOID             context;
        std::atomic<bool> armed;
        HANDLE            handle;
        struct timespec   timeoutAt;
        bool              hasTimeout;
        pthread_t         thread;
    };

    void* WorkRunner( void* arg )
    {
        auto* const w = static_cast<TpWork*>( arg );
        if ( !w->common.cancelled && w->callback )
        {
            w->callback( nullptr, w->context, reinterpret_cast<PTP_WORK>( w ) );
        }
        pthread_mutex_lock( &w->common.lock );
        --w->common.inflight;
        pthread_cond_broadcast( &w->common.cond );
        pthread_mutex_unlock( &w->common.lock );
        return nullptr;
    }

    void TimespecAddMs( struct timespec* ts, uint64_t ms )
    {
        ts->tv_sec  += ms / 1000;
        ts->tv_nsec += ( ms % 1000 ) * 1000000;
        if ( ts->tv_nsec >= 1000000000 ) { ts->tv_sec += 1; ts->tv_nsec -= 1000000000; }
    }

    void* TimerRunner( void* arg )
    {
        auto* const t = static_cast<TpTimer*>( arg );
        // Sleep until fireAt.
        clock_nanosleep( CLOCK_MONOTONIC, TIMER_ABSTIME, &t->fireAt, nullptr );
        if ( t->armed.load() && !t->common.cancelled && t->callback )
        {
            t->callback( nullptr, t->context, reinterpret_cast<PTP_TIMER>( t ) );
        }
        pthread_mutex_lock( &t->common.lock );
        --t->common.inflight;
        pthread_cond_broadcast( &t->common.cond );
        pthread_mutex_unlock( &t->common.lock );
        return nullptr;
    }

    void* WaitRunner( void* arg )
    {
        auto* const w = static_cast<TpWait*>( arg );
        DWORD timeoutMs = INFINITE;
        if ( w->hasTimeout )
        {
            struct timespec now;
            clock_gettime( CLOCK_MONOTONIC, &now );
            const long ms = ( w->timeoutAt.tv_sec - now.tv_sec ) * 1000
                            + ( w->timeoutAt.tv_nsec - now.tv_nsec ) / 1000000;
            timeoutMs = ms > 0 ? static_cast<DWORD>( ms ) : 0;
        }
        const DWORD r = WaitForSingleObject( w->handle, timeoutMs );
        if ( w->armed.load() && !w->common.cancelled && w->callback )
        {
            w->callback( nullptr, w->context, reinterpret_cast<PTP_WAIT>( w ), r );
        }
        pthread_mutex_lock( &w->common.lock );
        --w->common.inflight;
        pthread_cond_broadcast( &w->common.cond );
        pthread_mutex_unlock( &w->common.lock );
        return nullptr;
    }
}

extern "C" {

PTP_POOL CreateThreadpool( PVOID /*reserved*/ )
{
    auto* const p = static_cast<TpPool*>( calloc( 1, sizeof( TpPool ) ) );
    if ( !p ) return nullptr;
    p->threadMin = 0;
    p->threadMax = 500;
    return reinterpret_cast<PTP_POOL>( p );
}

void CloseThreadpool( PTP_POOL ptpp )
{
    free( ptpp );
}

BOOL SetThreadpoolThreadMinimum( PTP_POOL ptpp, DWORD cthrdMic )
{
    auto* const p = reinterpret_cast<TpPool*>( ptpp );
    if ( !p ) return FALSE;
    p->threadMin = cthrdMic;
    return TRUE;
}

void SetThreadpoolThreadMaximum( PTP_POOL ptpp, DWORD cthrdMost )
{
    auto* const p = reinterpret_cast<TpPool*>( ptpp );
    if ( p ) p->threadMax = cthrdMost;
}

PTP_CLEANUP_GROUP CreateThreadpoolCleanupGroup( void )
{
    return reinterpret_cast<PTP_CLEANUP_GROUP>( calloc( 1, sizeof( TpCleanupGroup ) ) );
}

void CloseThreadpoolCleanupGroup( PTP_CLEANUP_GROUP ptpcg )
{
    free( ptpcg );
}

void CloseThreadpoolCleanupGroupMembers( PTP_CLEANUP_GROUP /*ptpcg*/, BOOL /*fCancelPending*/, PVOID /*pv*/ )
{
    // Engine tracks its own membership; this is a no-op in our minimal impl.
}

void InitializeThreadpoolEnvironment( PTP_CALLBACK_ENVIRON pcbe )
{
    if ( pcbe )
    {
        memset( pcbe, 0, sizeof( *pcbe ) );
        pcbe->Version = 3;
        pcbe->Size    = sizeof( *pcbe );
    }
}

void DestroyThreadpoolEnvironment( PTP_CALLBACK_ENVIRON /*pcbe*/ )
{
}

void SetThreadpoolCallbackPool( PTP_CALLBACK_ENVIRON pcbe, PTP_POOL ptpp )
{
    if ( pcbe ) pcbe->Pool = ptpp;
}

void SetThreadpoolCallbackCleanupGroup( PTP_CALLBACK_ENVIRON pcbe, PTP_CLEANUP_GROUP ptpcg,
                                        void ( *pfng )( PVOID, PVOID ) )
{
    if ( pcbe )
    {
        pcbe->CleanupGroup                = ptpcg;
        pcbe->CleanupGroupCancelCallback  = pfng;
    }
}

PTP_WORK CreateThreadpoolWork( PTP_WORK_CALLBACK pfnwk, PVOID pv, PTP_CALLBACK_ENVIRON /*pcbe*/ )
{
    auto* const w = static_cast<TpWork*>( calloc( 1, sizeof( TpWork ) ) );
    if ( !w ) return nullptr;
    TpCommonInit( &w->common );
    w->callback = pfnwk;
    w->context  = pv;
    return reinterpret_cast<PTP_WORK>( w );
}

void SubmitThreadpoolWork( PTP_WORK pwk )
{
    auto* const w = reinterpret_cast<TpWork*>( pwk );
    if ( !w ) return;
    pthread_mutex_lock( &w->common.lock );
    ++w->common.inflight;
    pthread_mutex_unlock( &w->common.lock );

    pthread_t th;
    if ( pthread_create( &th, nullptr, WorkRunner, w ) == 0 )
    {
        pthread_detach( th );
    }
    else
    {
        pthread_mutex_lock( &w->common.lock );
        --w->common.inflight;
        pthread_cond_broadcast( &w->common.cond );
        pthread_mutex_unlock( &w->common.lock );
    }
}

BOOL TrySubmitThreadpoolCallback( PTP_SIMPLE_CALLBACK pfns, PVOID pv, PTP_CALLBACK_ENVIRON /*pcbe*/ )
{
    if ( !pfns ) return FALSE;
    // Wrap the simple callback in a TP_WORK; engine never sees the
    // intermediate work object. Self-deletes after running.
    struct SimpleCtx { PTP_SIMPLE_CALLBACK fn; PVOID ctx; };
    auto* const sc = static_cast<SimpleCtx*>( malloc( sizeof( SimpleCtx ) ) );
    if ( !sc ) return FALSE;
    sc->fn = pfns; sc->ctx = pv;

    pthread_t th;
    auto runner = +[]( void* arg ) -> void*
    {
        auto* const s = static_cast<SimpleCtx*>( arg );
        s->fn( nullptr, s->ctx );
        free( s );
        return nullptr;
    };
    if ( pthread_create( &th, nullptr, runner, sc ) != 0 )
    {
        free( sc );
        return FALSE;
    }
    pthread_detach( th );
    return TRUE;
}

void WaitForThreadpoolWorkCallbacks( PTP_WORK pwk, BOOL fCancelPendingCallbacks )
{
    auto* const w = reinterpret_cast<TpWork*>( pwk );
    if ( !w ) return;
    if ( fCancelPendingCallbacks ) w->common.cancelled = true;
    TpCommonWait( &w->common );
}

void CloseThreadpoolWork( PTP_WORK pwk )
{
    auto* const w = reinterpret_cast<TpWork*>( pwk );
    if ( !w ) return;
    TpCommonWait( &w->common );
    TpCommonDestroy( &w->common );
    free( w );
}

PTP_TIMER CreateThreadpoolTimer( PTP_TIMER_CALLBACK pfnti, PVOID pv, PTP_CALLBACK_ENVIRON /*pcbe*/ )
{
    auto* const t = static_cast<TpTimer*>( calloc( 1, sizeof( TpTimer ) ) );
    if ( !t ) return nullptr;
    TpCommonInit( &t->common );
    t->callback = pfnti;
    t->context  = pv;
    t->armed.store( false );
    return reinterpret_cast<PTP_TIMER>( t );
}

void SetThreadpoolTimer( PTP_TIMER pti, PFILETIME pftDueTime, DWORD /*msPeriod*/, DWORD /*msWindowLength*/ )
{
    auto* const t = reinterpret_cast<TpTimer*>( pti );
    if ( !t ) return;
    if ( !pftDueTime )
    {
        // Cancel: just disarm; any in-flight runner exits without firing.
        t->armed.store( false );
        return;
    }
    // FILETIME with high bit set is a relative offset in 100-ns units.
    int64_t ft = ( static_cast<int64_t>( pftDueTime->dwHighDateTime ) << 32 )
                 | pftDueTime->dwLowDateTime;
    struct timespec now;
    clock_gettime( CLOCK_MONOTONIC, &now );
    if ( ft < 0 )
    {
        // Relative: -ft is 100-ns units in the future
        const uint64_t ms = static_cast<uint64_t>( -ft ) / 10000;
        t->fireAt = now;
        TimespecAddMs( &t->fireAt, ms );
    }
    else
    {
        // Absolute UTC: convert to monotonic by computing delta from realtime.
        struct timespec rt;
        clock_gettime( CLOCK_REALTIME, &rt );
        const uint64_t unixSec = ( static_cast<uint64_t>( ft ) / 10000000ULL ) - 11644473600ULL;
        const long unixNs = static_cast<long>( ( ft % 10000000 ) * 100 );
        const int64_t deltaSec = static_cast<int64_t>( unixSec ) - rt.tv_sec;
        const int64_t deltaNs  = unixNs - rt.tv_nsec;
        t->fireAt = now;
        t->fireAt.tv_sec  += deltaSec;
        t->fireAt.tv_nsec += deltaNs;
        if ( t->fireAt.tv_nsec < 0 )            { t->fireAt.tv_sec -= 1; t->fireAt.tv_nsec += 1000000000; }
        if ( t->fireAt.tv_nsec >= 1000000000 )  { t->fireAt.tv_sec += 1; t->fireAt.tv_nsec -= 1000000000; }
    }

    pthread_mutex_lock( &t->common.lock );
    ++t->common.inflight;
    pthread_mutex_unlock( &t->common.lock );

    t->armed.store( true );
    if ( pthread_create( &t->thread, nullptr, TimerRunner, t ) == 0 )
    {
        pthread_detach( t->thread );
    }
    else
    {
        t->armed.store( false );
        pthread_mutex_lock( &t->common.lock );
        --t->common.inflight;
        pthread_cond_broadcast( &t->common.cond );
        pthread_mutex_unlock( &t->common.lock );
    }
}

void WaitForThreadpoolTimerCallbacks( PTP_TIMER pti, BOOL fCancelPending )
{
    auto* const t = reinterpret_cast<TpTimer*>( pti );
    if ( !t ) return;
    if ( fCancelPending )
    {
        t->common.cancelled = true;
        t->armed.store( false );
    }
    TpCommonWait( &t->common );
}

void CloseThreadpoolTimer( PTP_TIMER pti )
{
    auto* const t = reinterpret_cast<TpTimer*>( pti );
    if ( !t ) return;
    t->armed.store( false );
    TpCommonWait( &t->common );
    TpCommonDestroy( &t->common );
    free( t );
}

PTP_WAIT CreateThreadpoolWait( PTP_WAIT_CALLBACK pfnwa, PVOID pv, PTP_CALLBACK_ENVIRON /*pcbe*/ )
{
    auto* const w = static_cast<TpWait*>( calloc( 1, sizeof( TpWait ) ) );
    if ( !w ) return nullptr;
    TpCommonInit( &w->common );
    w->callback = pfnwa;
    w->context  = pv;
    w->armed.store( false );
    return reinterpret_cast<PTP_WAIT>( w );
}

void SetThreadpoolWait( PTP_WAIT pwa, HANDLE h, PFILETIME pftTimeout )
{
    auto* const w = reinterpret_cast<TpWait*>( pwa );
    if ( !w ) return;
    if ( !h )
    {
        w->armed.store( false );
        return;
    }
    w->handle     = h;
    w->hasTimeout = ( pftTimeout != nullptr );
    if ( pftTimeout )
    {
        struct timespec now;
        clock_gettime( CLOCK_MONOTONIC, &now );
        const int64_t ft = ( static_cast<int64_t>( pftTimeout->dwHighDateTime ) << 32 )
                           | pftTimeout->dwLowDateTime;
        const uint64_t ms = ( ft < 0 ) ? static_cast<uint64_t>( -ft ) / 10000 : 0;
        w->timeoutAt = now;
        TimespecAddMs( &w->timeoutAt, ms );
    }

    pthread_mutex_lock( &w->common.lock );
    ++w->common.inflight;
    pthread_mutex_unlock( &w->common.lock );

    w->armed.store( true );
    if ( pthread_create( &w->thread, nullptr, WaitRunner, w ) == 0 )
    {
        pthread_detach( w->thread );
    }
    else
    {
        w->armed.store( false );
        pthread_mutex_lock( &w->common.lock );
        --w->common.inflight;
        pthread_cond_broadcast( &w->common.cond );
        pthread_mutex_unlock( &w->common.lock );
    }
}

void WaitForThreadpoolWaitCallbacks( PTP_WAIT pwa, BOOL fCancelPending )
{
    auto* const w = reinterpret_cast<TpWait*>( pwa );
    if ( !w ) return;
    if ( fCancelPending )
    {
        w->common.cancelled = true;
        w->armed.store( false );
    }
    TpCommonWait( &w->common );
}

void CloseThreadpoolWait( PTP_WAIT pwa )
{
    auto* const w = reinterpret_cast<TpWait*>( pwa );
    if ( !w ) return;
    w->armed.store( false );
    TpCommonWait( &w->common );
    TpCommonDestroy( &w->common );
    free( w );
}

}  // extern "C"
