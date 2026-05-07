// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// POSIX implementations of the Win32 time API surface declared in
// windows-shim/windows.h. FILETIME counts 100-ns intervals since
// 1601-01-01 UTC; UNIX time counts seconds since 1970-01-01 UTC. The
// constant 11644473600 seconds bridges the two epochs.

#include "osstd.hxx"

#include <time.h>
#include <sys/time.h>

namespace
{
    constexpr ULONGLONG c_FiletimeIntervalsPerSecond = 10000000ULL;       // 100-ns ticks/sec
    constexpr ULONGLONG c_FiletimeUnixEpochOffset    = 11644473600ULL;    // seconds 1601→1970

    inline ULONGLONG FiletimeFromUnixNs( time_t sec, long nsec )
    {
        return ( static_cast<ULONGLONG>( sec ) + c_FiletimeUnixEpochOffset )
                 * c_FiletimeIntervalsPerSecond
             + static_cast<ULONGLONG>( nsec ) / 100ULL;
    }

    inline void FiletimeToUnixNs( ULONGLONG ft, time_t* sec, long* nsec )
    {
        const ULONGLONG ftSec = ft / c_FiletimeIntervalsPerSecond;
        const ULONGLONG ftRem = ft % c_FiletimeIntervalsPerSecond;
        *sec  = static_cast<time_t>( ftSec - c_FiletimeUnixEpochOffset );
        *nsec = static_cast<long>( ftRem * 100ULL );
    }

    inline ULONGLONG PackFiletime( const FILETIME* p )
    {
        return ( static_cast<ULONGLONG>( p->dwHighDateTime ) << 32 )
             | static_cast<ULONGLONG>( p->dwLowDateTime );
    }

    inline void StoreFiletime( FILETIME* p, ULONGLONG ft )
    {
        p->dwLowDateTime  = static_cast<DWORD>( ft & 0xFFFFFFFFULL );
        p->dwHighDateTime = static_cast<DWORD>( ft >> 32 );
    }

    void TmToSystemtime( const struct tm& src, unsigned ms, SYSTEMTIME* dst )
    {
        dst->wYear         = static_cast<WORD>( src.tm_year + 1900 );
        dst->wMonth        = static_cast<WORD>( src.tm_mon + 1 );
        dst->wDayOfWeek    = static_cast<WORD>( src.tm_wday );
        dst->wDay          = static_cast<WORD>( src.tm_mday );
        dst->wHour         = static_cast<WORD>( src.tm_hour );
        dst->wMinute       = static_cast<WORD>( src.tm_min );
        dst->wSecond       = static_cast<WORD>( src.tm_sec );
        dst->wMilliseconds = static_cast<WORD>( ms );
    }
}

extern "C" {

void GetSystemTimeAsFileTime( LPFILETIME lpSystemTimeAsFileTime )
{
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    StoreFiletime( lpSystemTimeAsFileTime, FiletimeFromUnixNs( ts.tv_sec, ts.tv_nsec ) );
}

void GetSystemTime( LPSYSTEMTIME lpSystemTime )
{
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    struct tm utc;
    gmtime_r( &ts.tv_sec, &utc );
    TmToSystemtime( utc, ts.tv_nsec / 1000000, lpSystemTime );
}

void GetLocalTime( LPSYSTEMTIME lpSystemTime )
{
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    struct tm local;
    localtime_r( &ts.tv_sec, &local );
    TmToSystemtime( local, ts.tv_nsec / 1000000, lpSystemTime );
}

BOOL FileTimeToSystemTime( const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime )
{
    time_t sec;
    long   nsec;
    FiletimeToUnixNs( PackFiletime( lpFileTime ), &sec, &nsec );
    struct tm utc;
    if ( !gmtime_r( &sec, &utc ) )
    {
        return FALSE;
    }
    TmToSystemtime( utc, nsec / 1000000, lpSystemTime );
    return TRUE;
}

BOOL SystemTimeToFileTime( const SYSTEMTIME* lpSystemTime, LPFILETIME lpFileTime )
{
    struct tm utc = {};
    utc.tm_year = lpSystemTime->wYear - 1900;
    utc.tm_mon  = lpSystemTime->wMonth - 1;
    utc.tm_mday = lpSystemTime->wDay;
    utc.tm_hour = lpSystemTime->wHour;
    utc.tm_min  = lpSystemTime->wMinute;
    utc.tm_sec  = lpSystemTime->wSecond;
    const time_t sec = timegm( &utc );
    if ( sec == static_cast<time_t>( -1 ) )
    {
        return FALSE;
    }
    StoreFiletime( lpFileTime,
                   FiletimeFromUnixNs( sec, lpSystemTime->wMilliseconds * 1000000L ) );
    return TRUE;
}

BOOL FileTimeToLocalFileTime( const FILETIME* lpFileTime, LPFILETIME lpLocalFileTime )
{
    time_t sec;
    long   nsec;
    FiletimeToUnixNs( PackFiletime( lpFileTime ), &sec, &nsec );
    struct tm local;
    if ( !localtime_r( &sec, &local ) )
    {
        return FALSE;
    }
    // Re-encode as if the broken-down local fields were UTC. The result is
    // a FILETIME whose value differs from the input by the local UTC offset
    // — matching Win32 semantics.
    struct tm asutc = local;
    asutc.tm_isdst = 0;
    const time_t shifted = timegm( &asutc );
    if ( shifted == static_cast<time_t>( -1 ) )
    {
        return FALSE;
    }
    StoreFiletime( lpLocalFileTime, FiletimeFromUnixNs( shifted, nsec ) );
    return TRUE;
}

BOOL SystemTimeToTzSpecificLocalTime( const TIME_ZONE_INFORMATION* lpTimeZone,
                                      const SYSTEMTIME*            lpUniversalTime,
                                      LPSYSTEMTIME                 lpLocalTime )
{
    // Engine only ever passes NULL — use current system zone.
    if ( lpTimeZone || !lpUniversalTime || !lpLocalTime ) return FALSE;
    struct tm utc = {};
    utc.tm_year = lpUniversalTime->wYear - 1900;
    utc.tm_mon  = lpUniversalTime->wMonth - 1;
    utc.tm_mday = lpUniversalTime->wDay;
    utc.tm_hour = lpUniversalTime->wHour;
    utc.tm_min  = lpUniversalTime->wMinute;
    utc.tm_sec  = lpUniversalTime->wSecond;
    const time_t sec = timegm( &utc );
    if ( sec == static_cast<time_t>( -1 ) ) return FALSE;
    struct tm local;
    if ( !localtime_r( &sec, &local ) ) return FALSE;
    TmToSystemtime( local, lpUniversalTime->wMilliseconds, lpLocalTime );
    return TRUE;
}

BOOL LocalFileTimeToFileTime( const FILETIME* lpLocalFileTime, LPFILETIME lpFileTime )
{
    time_t sec;
    long   nsec;
    FiletimeToUnixNs( PackFiletime( lpLocalFileTime ), &sec, &nsec );
    struct tm asutc;
    if ( !gmtime_r( &sec, &asutc ) )
    {
        return FALSE;
    }
    asutc.tm_isdst = -1;
    const time_t shifted = mktime( &asutc );
    if ( shifted == static_cast<time_t>( -1 ) )
    {
        return FALSE;
    }
    StoreFiletime( lpFileTime, FiletimeFromUnixNs( shifted, nsec ) );
    return TRUE;
}

ULONGLONG GetTickCount64( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return static_cast<ULONGLONG>( ts.tv_sec ) * 1000ULL
         + static_cast<ULONGLONG>( ts.tv_nsec ) / 1000000ULL;
}

// osstd_.hxx renames GetTickCount → GetTickCount_is_protected_... to poison
// accidental layer violations. time.cxx legitimately #undef's the macro to
// reach the real timer; we have to do the same here so the definition emits
// the un-renamed symbol time.cxx links against.
#undef GetTickCount
DWORD GetTickCount( void )
{
    return static_cast<DWORD>( GetTickCount64() );
}

BOOL QueryPerformanceFrequency( LARGE_INTEGER* lpFrequency )
{
    if ( !lpFrequency ) return FALSE;
    // Report ticks-per-second matching CLOCK_MONOTONIC * 10 (100-ns granularity).
    lpFrequency->QuadPart = static_cast<LONGLONG>( c_FiletimeIntervalsPerSecond );
    return TRUE;
}

BOOL QueryPerformanceCounter( LARGE_INTEGER* lpPerformanceCount )
{
    if ( !lpPerformanceCount ) return FALSE;
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    lpPerformanceCount->QuadPart =
        static_cast<LONGLONG>( ts.tv_sec ) * static_cast<LONGLONG>( c_FiletimeIntervalsPerSecond )
      + static_cast<LONGLONG>( ts.tv_nsec ) / 100LL;
    return TRUE;
}

}  // extern "C"
