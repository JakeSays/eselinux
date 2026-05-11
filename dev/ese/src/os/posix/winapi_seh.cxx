// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Runtime side of the Linux SEH emulation declared in windows-shim/excpt.h.
//
// Strategy:
//   * Maintain a thread-local stack of _SehFrame's pushed by _SehGuard ctors.
//   * On first push, lazily install a process-wide SIGSEGV handler.
//   * The handler walks the TLS top frame.  If non-null: mark faulted = 1,
//     stash STATUS_ACCESS_VIOLATION as the exception code, and siglongjmp
//     back to sigsetjmp.  If null: chain to the previously-installed
//     handler so real crashes still produce normal behavior.
//   * _SehRaiseException() lets non-signal code (e.g. the engine's
//     enforce-failure hook) inject a synthetic exception into the top
//     frame with an arbitrary code + info pointer.
//
// Limitations:
//   * Only SIGSEGV is converted to STATUS_ACCESS_VIOLATION.  Other signals
//     (FPE / illegal instruction) aren't intercepted yet — add them when a
//     test needs them.
//   * GetExceptionInformation() always returns null on Linux; filters that
//     need to inspect register state won't work.  Filters that just check
//     ExceptionCode work via _SehTopExceptionCode().

#include "osstd.hxx"

#include <excpt.h>

#include <atomic>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

namespace
{

thread_local _SehFrame* tls_top = nullptr;

std::atomic< bool >     g_handler_installed{ false };
struct sigaction        g_old_segv;

void seh_sigsegv_handler( int sig, siginfo_t* info, void* ctx )
{
    _SehFrame* top = tls_top;
    if ( top != nullptr )
    {
        top->faulted       = 1;
        top->exceptionCode = STATUS_ACCESS_VIOLATION;
        top->exceptionInfo = nullptr;
        //  siglongjmp is async-signal-safe and unwinds straight out of
        //  the signal handler, returning the second arg from the matching
        //  sigsetjmp call.  Never returns.
        siglongjmp( top->env, 1 );
    }

    //  No frame: chain to whatever was registered before us.
    if ( ( g_old_segv.sa_flags & SA_SIGINFO ) != 0 )
    {
        if ( g_old_segv.sa_sigaction != nullptr )
        {
            g_old_segv.sa_sigaction( sig, info, ctx );
            return;
        }
    }
    else
    {
        if ( g_old_segv.sa_handler != SIG_DFL && g_old_segv.sa_handler != SIG_IGN )
        {
            g_old_segv.sa_handler( sig );
            return;
        }
    }

    //  Default disposition was DFL/IGN — restore and re-raise to get the
    //  usual core-dump / process-terminate behavior.
    sigaction( SIGSEGV, &g_old_segv, nullptr );
    raise( sig );
}

void install_handler()
{
    bool expected = false;
    if ( !g_handler_installed.compare_exchange_strong( expected, true ) )
    {
        return;
    }

    struct sigaction sa;
    memset( &sa, 0, sizeof( sa ) );
    sa.sa_sigaction = seh_sigsegv_handler;
    //  SA_NODEFER: allow re-entry from the handler so a fault inside the
    //  fault handler (or a chained handler) hits the default action rather
    //  than blocking forever.
    sa.sa_flags     = SA_SIGINFO | SA_NODEFER;
    sigemptyset( &sa.sa_mask );
    sigaction( SIGSEGV, &sa, &g_old_segv );
}

}  //  namespace

extern "C" void _SehPush( _SehFrame* f )
{
    install_handler();
    f->prev = tls_top;
    tls_top = f;
}

extern "C" void _SehPop( _SehFrame* f )
{
    //  Pop unconditionally — _SehGuard's RAII guarantees push/pop pairing.
    tls_top = f->prev;
}

extern "C" unsigned long _SehTopExceptionCode( void )
{
    _SehFrame* top = tls_top;
    return ( top != nullptr ) ? top->exceptionCode : 0;
}

extern "C" void* _SehTopExceptionInfo( void )
{
    _SehFrame* top = tls_top;
    return ( top != nullptr ) ? top->exceptionInfo : nullptr;
}

//  Thread-local backing storage for GetExceptionInformation()'s return
//  value.  Filters call this from inside __except where the top _SehFrame
//  is still on the chain; the synthetic EXCEPTION_RECORD + EXCEPTION_POINTERS
//  pair stays valid until the next __except on the same thread.
namespace
{

thread_local EXCEPTION_RECORD   tls_exc_record;
thread_local EXCEPTION_POINTERS tls_exc_pointers;

}  //  namespace

extern "C" EXCEPTION_POINTERS* GetExceptionInformation( void )
{
    _SehFrame* top = tls_top;
    if ( top == nullptr )
    {
        return nullptr;
    }
    memset( &tls_exc_record, 0, sizeof( tls_exc_record ) );
    tls_exc_record.ExceptionCode          = top->exceptionCode;
    tls_exc_record.NumberParameters       = ( top->exceptionInfo != nullptr ) ? 1u : 0u;
    tls_exc_record.ExceptionInformation[ 0 ] = (unsigned long long)(uintptr_t)top->exceptionInfo;

    tls_exc_pointers.ExceptionRecord = &tls_exc_record;
    tls_exc_pointers.ContextRecord   = nullptr;
    return &tls_exc_pointers;
}

extern "C" unsigned long GetExceptionCode( void )
{
    _SehFrame* top = tls_top;
    return ( top != nullptr ) ? top->exceptionCode : 0;
}

extern "C" void _SehRaiseException( unsigned long exceptionCode, void* exceptionInfo )
{
    _SehFrame* top = tls_top;
    if ( top != nullptr )
    {
        top->faulted       = 1;
        top->exceptionCode = exceptionCode;
        top->exceptionInfo = exceptionInfo;
        siglongjmp( top->env, 1 );
        //  unreachable
    }
    //  No __try frame to catch this — same fate as an uncaught exception
    //  on Windows: terminate.
    abort();
}
