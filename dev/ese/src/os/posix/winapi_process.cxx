// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Process / thread identity, sleep, yield, and pseudo-handles for the
// current thread / process. The actual thread-management entry points
// (CreateThread, GetExitCodeThread, ...) live in winapi_thread.cxx.

#include "osstd.hxx"

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern "C" {

DWORD GetCurrentProcessId( void )
{
    return static_cast<DWORD>( getpid() );
}

DWORD GetCurrentThreadId( void )
{
    // gettid() is the kernel thread id (TID), matching the Win32 contract
    // that GetCurrentThreadId returns a per-thread integer unique within
    // the process. glibc < 2.30 didn't wrap it; use the syscall directly.
    return static_cast<DWORD>( syscall( SYS_gettid ) );
}

HANDLE GetCurrentThread( void )
{
    // Win32 returns a pseudo-handle that resolves to the calling thread.
    // Use a fixed sentinel; any code that needs a real waitable HANDLE
    // must DuplicateHandle into one (handled in winapi_thread.cxx).
    return reinterpret_cast<HANDLE>( static_cast<intptr_t>( -2 ) );
}

HANDLE GetCurrentProcess( void )
{
    return reinterpret_cast<HANDLE>( static_cast<intptr_t>( -1 ) );
}

void Sleep( DWORD dwMilliseconds )
{
    if ( dwMilliseconds == 0 )
    {
        sched_yield();
        return;
    }
    struct timespec ts;
    ts.tv_sec  = dwMilliseconds / 1000;
    ts.tv_nsec = static_cast<long>( dwMilliseconds % 1000 ) * 1000000L;
    while ( nanosleep( &ts, &ts ) == -1 && errno == EINTR )
    {
    }
}

DWORD SleepEx( DWORD dwMilliseconds, BOOL /*bAlertable*/ )
{
    // No APCs on Linux — alertable wait collapses to plain sleep.
    Sleep( dwMilliseconds );
    return 0;  // WAIT_OBJECT_0 / non-alertable completion
}

void SwitchToThread( void )
{
    sched_yield();
}

BOOL IsProcessorFeaturePresent( DWORD ProcessorFeature )
{
    // Engine only ever asks about RDTSC, which is universal on x86_64.
    return ProcessorFeature == PF_RDTSC_INSTRUCTION_AVAILABLE ? TRUE : FALSE;
}

}  // extern "C"
