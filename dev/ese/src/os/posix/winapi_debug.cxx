// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// OutputDebugStringA/W route to stderr for v1 (LTTng tracepoint when that
// pipeline lands). DebugBreak / IsDebuggerPresent use the standard Linux
// ptrace check + SIGTRAP raise.

#include "osstd.hxx"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" {

void OutputDebugStringA( LPCSTR lpOutputString )
{
    if ( !lpOutputString )
    {
        return;
    }
    const size_t cb = strlen( lpOutputString );
    ssize_t written = 0;
    while ( static_cast<size_t>( written ) < cb )
    {
        const ssize_t n = write( STDERR_FILENO,
                                 lpOutputString + written,
                                 cb - static_cast<size_t>( written ) );
        if ( n < 0 )
        {
            if ( errno == EINTR ) continue;
            return;
        }
        written += n;
    }
}

void OutputDebugStringW( LPCWSTR lpOutputString )
{
    // Lossy wide→narrow for v1: take the low byte if codepoint < 0x80,
    // emit '?' otherwise. Good enough for diagnostic strings; replace with
    // iconv when winapi_nls.cxx lands and the engine actually traces wide
    // strings in production.
    if ( !lpOutputString )
    {
        return;
    }
    char buf[ 256 ];
    size_t cb = 0;
    for ( const WCHAR* p = lpOutputString; *p; ++p )
    {
        const wchar_t wc = *p;
        const char    c  = ( wc < 0x80 ) ? static_cast<char>( wc ) : '?';
        buf[ cb++ ] = c;
        if ( cb == sizeof( buf ) )
        {
            (void)!write( STDERR_FILENO, buf, cb );
            cb = 0;
        }
    }
    if ( cb )
    {
        (void)!write( STDERR_FILENO, buf, cb );
    }
}

void DebugBreak( void )
{
    raise( SIGTRAP );
}

[[noreturn]] void RaiseFailFastException( struct _EXCEPTION_RECORD* /*pExceptionRecord*/,
                                          struct _CONTEXT*          /*pContextRecord*/,
                                          DWORD                     /*dwFlags*/ )
{
    // Win32 fast-fail terminates the process without running cleanup
    // handlers. abort() is the closest POSIX equivalent: it raises
    // SIGABRT which the default action turns into process death.
    abort();
}

BOOL IsDebuggerPresent( void )
{
    // Standard Linux idiom: scan /proc/self/status for a non-zero TracerPid.
    const int fd = open( "/proc/self/status", O_RDONLY | O_CLOEXEC );
    if ( fd < 0 )
    {
        return FALSE;
    }
    char buf[ 4096 ];
    const ssize_t n = read( fd, buf, sizeof( buf ) - 1 );
    close( fd );
    if ( n <= 0 )
    {
        return FALSE;
    }
    buf[ n ] = '\0';
    const char* p = strstr( buf, "TracerPid:" );
    if ( !p )
    {
        return FALSE;
    }
    p += sizeof( "TracerPid:" ) - 1;
    while ( *p == ' ' || *p == '\t' ) ++p;
    return ( *p != '0' ) ? TRUE : FALSE;
}

}  // extern "C"
