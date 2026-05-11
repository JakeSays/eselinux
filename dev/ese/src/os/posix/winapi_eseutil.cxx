// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Stubs / minimal POSIX implementations for the small set of CRT and
// Win32 entry points that eseutil reaches for but the rest of the engine
// does not need: GetSystemInfo, CopyFileExW, _wfullpath, wcscpy_s,
// wcscat_s, _snwscanf_s.
//
// These don't belong with libese.so's core surface, but living here lets
// eseutil link against libosposix.a without growing a separate utility
// library.

#include "osstd.hxx"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


// ---- GetSystemInfo --------------------------------------------------------
//
// Engine call sites only consume dwPageSize / dwNumberOfProcessors.

extern "C" void GetSystemInfo( LPSYSTEM_INFO si )
{
    if ( !si ) return;
    memset( si, 0, sizeof( *si ) );
    si->dwPageSize             = (DWORD)sysconf( _SC_PAGESIZE );
    si->dwAllocationGranularity = si->dwPageSize;
    long n = sysconf( _SC_NPROCESSORS_ONLN );
    si->dwNumberOfProcessors   = ( n > 0 ) ? (DWORD)n : 1;
}


// ---- CopyFileExW ----------------------------------------------------------
//
// eseutil's `/y` mode copies one file to another, optionally through a
// progress callback. We translate the wide source/dest names to UTF-8 and
// drive sendfile() in a loop. The progress callback, if supplied, is invoked
// at chunk boundaries so the caller can render its UI; STREAM_SWITCH is sent
// once on entry. Cancellation honors *pbCancel and a callback return of
// PROGRESS_CANCEL/PROGRESS_STOP.

namespace
{
    bool WideToUtf8( const wchar_t* src, char* out, size_t cbOut )
    {
        if ( !src || !out || cbOut == 0 ) return false;
        size_t i = 0;
        for ( const wchar_t* p = src; *p; ++p )
        {
            wchar_t w = *p;
            if ( w < 0x80 )
            {
                if ( i + 1 >= cbOut ) return false;
                out[ i++ ] = (char)w;
            }
            else if ( w < 0x800 )
            {
                if ( i + 2 >= cbOut ) return false;
                out[ i++ ] = (char)( 0xC0 | ( w >> 6 ) );
                out[ i++ ] = (char)( 0x80 | ( w & 0x3F ) );
            }
            else
            {
                if ( i + 3 >= cbOut ) return false;
                out[ i++ ] = (char)( 0xE0 | ( w >> 12 ) );
                out[ i++ ] = (char)( 0x80 | ( ( w >> 6 ) & 0x3F ) );
                out[ i++ ] = (char)( 0x80 | ( w & 0x3F ) );
            }
        }
        out[ i ] = '\0';
        return true;
    }
}

extern "C" BOOL CopyFileExW(
    const wchar_t*     lpExistingFileName,
    const wchar_t*     lpNewFileName,
    LPPROGRESS_ROUTINE lpProgressRoutine,
    LPVOID             lpData,
    LPBOOL             pbCancel,
    DWORD              dwCopyFlags )
{
    char szSrc[ PATH_MAX ];
    char szDst[ PATH_MAX ];
    if ( !WideToUtf8( lpExistingFileName, szSrc, sizeof( szSrc ) ) ) return FALSE;
    if ( !WideToUtf8( lpNewFileName,      szDst, sizeof( szDst ) ) ) return FALSE;

    int oflags = O_WRONLY | O_CREAT | O_TRUNC;
    if ( dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS ) oflags |= O_EXCL;

    int fdSrc = open( szSrc, O_RDONLY );
    if ( fdSrc < 0 ) return FALSE;

    struct stat st;
    if ( fstat( fdSrc, &st ) != 0 ) { close( fdSrc ); return FALSE; }

    int fdDst = open( szDst, oflags, st.st_mode & 0777 );
    if ( fdDst < 0 ) { close( fdSrc ); return FALSE; }

    LARGE_INTEGER liTotal;       liTotal.QuadPart       = st.st_size;
    LARGE_INTEGER liStreamSize;  liStreamSize.QuadPart  = st.st_size;
    LARGE_INTEGER liTransferred; liTransferred.QuadPart = 0;

    if ( lpProgressRoutine )
    {
        DWORD r = lpProgressRoutine( liTotal, liTransferred, liStreamSize, liTransferred,
                                     1, CALLBACK_STREAM_SWITCH, (HANDLE)(intptr_t)fdSrc,
                                     (HANDLE)(intptr_t)fdDst, lpData );
        if ( r == PROGRESS_CANCEL || r == PROGRESS_STOP )
        {
            close( fdSrc );
            close( fdDst );
            unlink( szDst );
            return FALSE;
        }
    }

    off_t off = 0;
    while ( off < st.st_size )
    {
        if ( pbCancel && *pbCancel )
        {
            close( fdSrc );
            close( fdDst );
            unlink( szDst );
            return FALSE;
        }

        size_t  cbChunk = (size_t)( st.st_size - off );
        if ( cbChunk > ( 1u << 20 ) ) cbChunk = ( 1u << 20 );
        ssize_t n = sendfile( fdDst, fdSrc, &off, cbChunk );
        if ( n < 0 )
        {
            if ( errno == EINTR ) continue;
            close( fdSrc ); close( fdDst ); unlink( szDst );
            return FALSE;
        }

        liTransferred.QuadPart = off;

        if ( lpProgressRoutine )
        {
            DWORD r = lpProgressRoutine( liTotal, liTransferred, liStreamSize, liTransferred,
                                         1, CALLBACK_CHUNK_FINISHED, (HANDLE)(intptr_t)fdSrc,
                                         (HANDLE)(intptr_t)fdDst, lpData );
            if ( r == PROGRESS_CANCEL || r == PROGRESS_STOP )
            {
                close( fdSrc ); close( fdDst ); unlink( szDst );
                return FALSE;
            }
        }
    }

    close( fdSrc );
    close( fdDst );
    return TRUE;
}


// ---- _wfullpath -----------------------------------------------------------
//
// Resolves a (possibly relative) wide path against the current working
// directory. Unlike POSIX realpath() the Windows function does NOT require
// the file to exist; eseutil uses it to canonicalize candidate db names
// before they're created. Implementation: if the path is already absolute,
// just copy it; otherwise prepend cwd.

extern "C" wchar_t* _wfullpath( wchar_t* absPath, const wchar_t* relPath, size_t cchMax )
{
    if ( !relPath || cchMax == 0 ) return nullptr;

    static thread_local wchar_t s_buf[ PATH_MAX ];
    wchar_t* dst = absPath ? absPath : s_buf;
    size_t   cap = absPath ? cchMax  : ( sizeof( s_buf ) / sizeof( s_buf[ 0 ] ) );

    auto AppendNarrow = [&]( const char* sz, size_t& i ) -> bool
    {
        for ( ; *sz; ++sz )
        {
            if ( i + 1 >= cap ) return false;
            dst[ i++ ] = (wchar_t)(unsigned char)*sz;
        }
        return true;
    };

    auto AppendWide = [&]( const wchar_t* w, size_t& i ) -> bool
    {
        for ( ; *w; ++w )
        {
            if ( i + 1 >= cap ) return false;
            dst[ i++ ] = *w;
        }
        return true;
    };

    size_t i = 0;
    if ( relPath[ 0 ] != L'/' )
    {
        char szCwd[ PATH_MAX ];
        if ( !getcwd( szCwd, sizeof( szCwd ) ) ) return nullptr;
        if ( !AppendNarrow( szCwd, i ) ) return nullptr;
        if ( i + 1 >= cap ) return nullptr;
        dst[ i++ ] = L'/';
    }
    if ( !AppendWide( relPath, i ) ) return nullptr;
    dst[ i ] = L'\0';
    return dst;
}


// ---- wcscpy_s / wcscat_s --------------------------------------------------

extern "C" errno_t wcscpy_s( wchar_t* dst, size_t cchDst, const wchar_t* src )
{
    if ( !dst || cchDst == 0 )                     return EINVAL;
    if ( !src ) { dst[ 0 ] = L'\0'; return EINVAL; }
    size_t i = 0;
    while ( src[ i ] && i + 1 < cchDst ) { dst[ i ] = src[ i ]; ++i; }
    dst[ i ] = L'\0';
    return src[ i ] ? ERANGE : 0;
}

extern "C" errno_t wcscat_s( wchar_t* dst, size_t cchDst, const wchar_t* src )
{
    if ( !dst || cchDst == 0 || !src ) return EINVAL;
    size_t lenDst = 0;
    while ( lenDst < cchDst && dst[ lenDst ] ) ++lenDst;
    if ( lenDst == cchDst ) return EINVAL;  // unterminated
    return wcscpy_s( dst + lenDst, cchDst - lenDst, src );
}

extern "C" errno_t strcpy_s( char* dst, size_t cchDst, const char* src )
{
    if ( !dst || cchDst == 0 )                     return EINVAL;
    if ( !src ) { dst[ 0 ] = '\0'; return EINVAL; }
    size_t i = 0;
    while ( src[ i ] && i + 1 < cchDst ) { dst[ i ] = src[ i ]; ++i; }
    dst[ i ] = '\0';
    return src[ i ] ? ERANGE : 0;
}

extern "C" errno_t strcat_s( char* dst, size_t cchDst, const char* src )
{
    if ( !dst || cchDst == 0 || !src ) return EINVAL;
    size_t lenDst = 0;
    while ( lenDst < cchDst && dst[ lenDst ] ) ++lenDst;
    if ( lenDst == cchDst ) return EINVAL;
    return strcpy_s( dst + lenDst, cchDst - lenDst, src );
}

extern "C" errno_t _wcsupr_s( wchar_t* str, size_t cchStr )
{
    if ( !str || cchStr == 0 ) return EINVAL;
    size_t i = 0;
    while ( i < cchStr && str[ i ] )
    {
        wchar_t c = str[ i ];
        if ( c >= L'a' && c <= L'z' ) str[ i ] = (wchar_t)( c - L'a' + L'A' );
        ++i;
    }
    return ( i == cchStr ) ? EINVAL : 0;
}


// ---- _snwscanf_s ----------------------------------------------------------
//
// Subset implementation: walks the format string, matches literal characters,
// and accepts %d / %u / %ld / %lu / %I64d / %I64u / %c. Sufficient for
// eseutil's option parsing. Returns the number of fields successfully
// assigned, or EOF (-1) on input exhaustion before the first conversion.

namespace
{
    bool ScanInt64( const wchar_t*& p, long long& out, bool unsignedOnly )
    {
        while ( *p == L' ' || *p == L'\t' ) ++p;
        bool neg = false;
        if ( !unsignedOnly && ( *p == L'-' || *p == L'+' ) ) { neg = ( *p == L'-' ); ++p; }
        if ( *p < L'0' || *p > L'9' ) return false;
        long long v = 0;
        while ( *p >= L'0' && *p <= L'9' ) { v = v * 10 + ( *p - L'0' ); ++p; }
        out = neg ? -v : v;
        return true;
    }
}

extern "C" int _snwscanf_s( const wchar_t* buf, size_t /*cchCount*/, const wchar_t* fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    int          cAssigned = 0;
    const wchar_t* p = buf;
    const wchar_t* f = fmt;

    while ( *f )
    {
        if ( *f == L' ' || *f == L'\t' )
        {
            while ( *p == L' ' || *p == L'\t' ) ++p;
            ++f;
            continue;
        }
        if ( *f != L'%' )
        {
            if ( *p != *f ) break;
            ++p; ++f;
            continue;
        }

        ++f;  // skip '%'
        bool isLong = false;
        bool isLL   = false;
        if ( f[ 0 ] == L'I' && f[ 1 ] == L'6' && f[ 2 ] == L'4' ) { isLL = true; f += 3; }
        else if ( *f == L'l' ) { ++f; if ( *f == L'l' ) { isLL = true; ++f; } else isLong = true; }

        wchar_t conv = *f++;
        switch ( conv )
        {
            case L'd':
            case L'u':
            {
                long long v = 0;
                if ( !ScanInt64( p, v, conv == L'u' ) ) goto done;
                if ( isLL )    *va_arg( args, long long* )    = v;
                else if ( isLong ) *va_arg( args, long* )      = (long)v;
                else              *va_arg( args, int* )        = (int)v;
                ++cAssigned;
                break;
            }
            case L'c':
            {
                if ( *p == L'\0' ) goto done;
                *va_arg( args, wchar_t* ) = *p++;
                // Caller passes a buffer-size arg under -D_CRT_SECURE_CPP_OVERLOAD;
                // _snwscanf_s consumes it but we ignore it.
                (void)va_arg( args, size_t );
                ++cAssigned;
                break;
            }
            default:
                goto done;
        }
    }

done:
    va_end( args );
    return cAssigned;
}
