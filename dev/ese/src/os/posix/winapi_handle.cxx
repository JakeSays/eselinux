// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// HANDLE infrastructure: lifecycle (Close/Duplicate), handle flags, and
// the shared kernel-object allocator used by events/mutexes/semaphores/
// threads. The actual primitive bodies live in winapi_sync.cxx and
// winapi_thread.cxx.
//
// CurrentProcess / CurrentThread sentinels (returned by GetCurrentProcess
// and GetCurrentThread) are -1 / -2 — those bypass the KObject path
// entirely; CloseHandle on them is a no-op.

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

using osposix::AllocKObject;
using osposix::FreeKObject;
using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;

namespace
{
    constexpr intptr_t c_currentProcessSentinel = -1;
    constexpr intptr_t c_currentThreadSentinel  = -2;

    inline bool IsPseudoHandle( HANDLE h )
    {
        const intptr_t v = reinterpret_cast<intptr_t>( h );
        return v == c_currentProcessSentinel || v == c_currentThreadSentinel;
    }
}

namespace osposix
{

KObject* AllocKObject( HandleKind kind )
{
    auto* const k = static_cast<KObject*>( calloc( 1, sizeof( KObject ) ) );
    if ( !k )
    {
        return nullptr;
    }
    k->kind = kind;
    k->refCount.store( 1, std::memory_order_relaxed );

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init( &mattr );
    pthread_mutexattr_settype( &mattr, PTHREAD_MUTEX_NORMAL );
    pthread_mutex_init( &k->lock, &mattr );
    pthread_mutexattr_destroy( &mattr );

    pthread_condattr_t cattr;
    pthread_condattr_init( &cattr );
    pthread_condattr_setclock( &cattr, CLOCK_MONOTONIC );
    pthread_cond_init( &k->cond, &cattr );
    pthread_condattr_destroy( &cattr );

    return k;
}

void FreeKObject( KObject* k )
{
    if ( !k ) return;
    if ( k->kind == HandleKind::File )
    {
        if ( k->fileFd >= 0 ) close( k->fileFd );
        if ( k->fileDeleteOnClosePath )
        {
            unlink( k->fileDeleteOnClosePath );
            free( k->fileDeleteOnClosePath );
        }
        free( k->fileOpenedPath );
    }
    else if ( k->kind == HandleKind::FindFile || k->kind == HandleKind::FindVolume )
    {
        if ( k->findDir ) closedir( static_cast<DIR*>( k->findDir ) );
        free( k->findBaseDir );
        free( k->findPattern );
    }
    pthread_cond_destroy( &k->cond );
    pthread_mutex_destroy( &k->lock );
    free( k );
}

}  // namespace osposix

extern "C" {

BOOL CloseHandle( HANDLE hObject )
{
    if ( !hObject || IsPseudoHandle( hObject ) )
    {
        return TRUE;
    }
    KObject* const k = HandleToK( hObject );
    if ( k->refCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
    {
        FreeKObject( k );
    }
    return TRUE;
}

BOOL DuplicateHandle( HANDLE /*hSourceProcessHandle*/, HANDLE hSourceHandle,
                      HANDLE /*hTargetProcessHandle*/, LPHANDLE lpTargetHandle,
                      DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, DWORD dwOptions )
{
    if ( !lpTargetHandle )
    {
        return FALSE;
    }

    if ( IsPseudoHandle( hSourceHandle ) )
    {
        // Win32 contract: duplicating GetCurrentThread() yields a real
        // waitable handle to that thread. v1: just hand the sentinel
        // back; engine code uses it only to keep handle protocols flowing.
        *lpTargetHandle = hSourceHandle;
        return TRUE;
    }

    KObject* const k = HandleToK( hSourceHandle );
    k->refCount.fetch_add( 1, std::memory_order_relaxed );
    *lpTargetHandle = hSourceHandle;

    if ( dwOptions & DUPLICATE_CLOSE_SOURCE )
    {
        CloseHandle( hSourceHandle );
    }
    return TRUE;
}

BOOL GetHandleInformation( HANDLE /*hObject*/, LPDWORD lpdwFlags )
{
    if ( lpdwFlags ) *lpdwFlags = 0;
    return TRUE;
}

BOOL SetHandleInformation( HANDLE /*hObject*/, DWORD /*dwMask*/, DWORD /*dwFlags*/ )
{
    // Linux fd-inheritance / protect-from-close is filesystem-level
    // (FD_CLOEXEC). Engine sets HANDLE_FLAG_PROTECT_FROM_CLOSE on a few
    // long-lived handles purely as belt-and-suspenders against double
    // close — accept and ignore.
    return TRUE;
}

}  // extern "C"
