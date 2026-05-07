// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// MultiByteToWideChar / WideCharToMultiByte. The engine only ever asks
// for CP_ACP, CP_UTF8, or CP_THREAD_ACP — all of which we treat as
// UTF-8 on Linux. WCHAR is 16-bit (UTF-16) under -fshort-wchar; we
// hand-roll the UTF-8↔UTF-16 conversion rather than dragging in iconv,
// since the codepoint range is what matters and the format is fixed.
//
// Win32 length contract:
//   cbMultiByte == -1 / cchWideChar == -1: input is null-terminated;
//      output count includes the terminating null.
//   cbMultiByte / cchWideChar > 0: explicit byte/char count; output
//      count does NOT include a terminator (caller didn't ask).
//   cchWideChar / cbMultiByte == 0: output buffer is sizing query;
//      return required size, do not write.

#include "osstd.hxx"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace
{
    // Decode one UTF-8 codepoint from [p, end). Returns codepoint
    // (U+FFFD on malformed input) and advances p past the consumed bytes.
    uint32_t DecodeUtf8( const unsigned char*& p, const unsigned char* end )
    {
        if ( p >= end )
        {
            return 0;
        }
        const unsigned char b0 = *p++;
        uint32_t cp;
        int extra;
        if ( b0 < 0x80 )       { return b0; }
        else if ( b0 < 0xC0 )  { return 0xFFFD; }   // stray continuation
        else if ( b0 < 0xE0 )  { cp = b0 & 0x1F; extra = 1; }
        else if ( b0 < 0xF0 )  { cp = b0 & 0x0F; extra = 2; }
        else if ( b0 < 0xF8 )  { cp = b0 & 0x07; extra = 3; }
        else                   { return 0xFFFD; }
        for ( int i = 0; i < extra; ++i )
        {
            if ( p >= end || ( *p & 0xC0 ) != 0x80 )
            {
                return 0xFFFD;
            }
            cp = ( cp << 6 ) | ( *p++ & 0x3F );
        }
        return cp;
    }

    // Encode codepoint into UTF-16. Returns # of WCHARs (1 or 2).
    int EncodeUtf16( uint32_t cp, WCHAR* out )
    {
        if ( cp <= 0xFFFF )
        {
            if ( out ) out[ 0 ] = static_cast<WCHAR>( cp );
            return 1;
        }
        if ( cp > 0x10FFFF )
        {
            cp = 0xFFFD;
            if ( out ) out[ 0 ] = static_cast<WCHAR>( cp );
            return 1;
        }
        cp -= 0x10000;
        if ( out )
        {
            out[ 0 ] = static_cast<WCHAR>( 0xD800 | ( cp >> 10 ) );
            out[ 1 ] = static_cast<WCHAR>( 0xDC00 | ( cp & 0x3FF ) );
        }
        return 2;
    }

    // Decode one UTF-16 codepoint, advancing p.
    uint32_t DecodeUtf16( const WCHAR*& p, const WCHAR* end )
    {
        if ( p >= end )
        {
            return 0;
        }
        const uint32_t hi = *p++;
        if ( hi >= 0xD800 && hi <= 0xDBFF && p < end )
        {
            const uint32_t lo = *p;
            if ( lo >= 0xDC00 && lo <= 0xDFFF )
            {
                ++p;
                return 0x10000 + ( ( hi - 0xD800 ) << 10 ) + ( lo - 0xDC00 );
            }
        }
        return hi;
    }

    int EncodeUtf8( uint32_t cp, char* out )
    {
        if ( cp < 0x80 )
        {
            if ( out ) out[ 0 ] = static_cast<char>( cp );
            return 1;
        }
        if ( cp < 0x800 )
        {
            if ( out )
            {
                out[ 0 ] = static_cast<char>( 0xC0 | ( cp >> 6 ) );
                out[ 1 ] = static_cast<char>( 0x80 | ( cp & 0x3F ) );
            }
            return 2;
        }
        if ( cp < 0x10000 )
        {
            if ( out )
            {
                out[ 0 ] = static_cast<char>( 0xE0 | ( cp >> 12 ) );
                out[ 1 ] = static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
                out[ 2 ] = static_cast<char>( 0x80 | ( cp & 0x3F ) );
            }
            return 3;
        }
        if ( out )
        {
            out[ 0 ] = static_cast<char>( 0xF0 | ( cp >> 18 ) );
            out[ 1 ] = static_cast<char>( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
            out[ 2 ] = static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
            out[ 3 ] = static_cast<char>( 0x80 | ( cp & 0x3F ) );
        }
        return 4;
    }
}

extern "C" {

int MultiByteToWideChar( UINT /*CodePage*/, DWORD /*dwFlags*/,
                         LPCSTR lpMultiByteStr, int cbMultiByte,
                         LPWSTR lpWideCharStr, int cchWideChar )
{
    if ( !lpMultiByteStr )
    {
        return 0;
    }

    const bool nullTerminated = ( cbMultiByte == -1 );
    size_t cb;
    if ( nullTerminated )
    {
        cb = 0;
        while ( lpMultiByteStr[ cb ] ) ++cb;
        ++cb;  // include terminating null in conversion
    }
    else if ( cbMultiByte < 0 )
    {
        return 0;
    }
    else
    {
        cb = static_cast<size_t>( cbMultiByte );
    }

    const auto* const begin = reinterpret_cast<const unsigned char*>( lpMultiByteStr );
    const auto* const end   = begin + cb;
    const auto*       p     = begin;

    int produced = 0;
    while ( p < end )
    {
        const unsigned char b = *p;
        uint32_t cp;
        if ( b == 0 && nullTerminated )
        {
            // Encode the trailing null as a single 0 WCHAR and stop.
            cp = 0;
            ++p;
        }
        else
        {
            cp = DecodeUtf8( p, end );
        }

        WCHAR* dst = nullptr;
        if ( cchWideChar > 0 )
        {
            if ( produced >= cchWideChar )
            {
                return 0;  // insufficient buffer; Win32 returns 0 + sets ERROR_INSUFFICIENT_BUFFER
            }
            dst = lpWideCharStr + produced;
        }
        const int wrote = EncodeUtf16( cp, dst );
        produced += wrote;

        if ( cp == 0 && nullTerminated ) break;
    }
    return produced;
}

int WideCharToMultiByte( UINT /*CodePage*/, DWORD /*dwFlags*/,
                         LPCWSTR lpWideCharStr, int cchWideChar,
                         LPSTR lpMultiByteStr, int cbMultiByte,
                         LPCSTR /*lpDefaultChar*/, LPBOOL lpUsedDefaultChar )
{
    if ( lpUsedDefaultChar ) *lpUsedDefaultChar = FALSE;
    if ( !lpWideCharStr )
    {
        return 0;
    }

    const bool nullTerminated = ( cchWideChar == -1 );
    size_t cch;
    if ( nullTerminated )
    {
        cch = 0;
        while ( lpWideCharStr[ cch ] ) ++cch;
        ++cch;
    }
    else if ( cchWideChar < 0 )
    {
        return 0;
    }
    else
    {
        cch = static_cast<size_t>( cchWideChar );
    }

    const WCHAR* const begin = lpWideCharStr;
    const WCHAR* const end   = begin + cch;
    const WCHAR*       p     = begin;

    int produced = 0;
    while ( p < end )
    {
        uint32_t cp;
        if ( *p == 0 && nullTerminated )
        {
            cp = 0;
            ++p;
        }
        else
        {
            cp = DecodeUtf16( p, end );
        }

        char buf[ 4 ];
        const int wrote = EncodeUtf8( cp, buf );
        if ( cbMultiByte > 0 )
        {
            if ( produced + wrote > cbMultiByte )
            {
                return 0;
            }
            for ( int i = 0; i < wrote; ++i )
            {
                lpMultiByteStr[ produced + i ] = buf[ i ];
            }
        }
        produced += wrote;
        if ( cp == 0 && nullTerminated ) break;
    }
    return produced;
}

namespace
{
    int FormatViaStrftime( const SYSTEMTIME* st, const char* pattern,
                           LPWSTR out, int cchOut )
    {
        if ( !st || !pattern ) return 0;
        struct tm bd = {};
        bd.tm_year = st->wYear - 1900;
        bd.tm_mon  = st->wMonth - 1;
        bd.tm_mday = st->wDay;
        bd.tm_hour = st->wHour;
        bd.tm_min  = st->wMinute;
        bd.tm_sec  = st->wSecond;
        bd.tm_wday = st->wDayOfWeek;
        char buf[ 128 ];
        const size_t n = strftime( buf, sizeof( buf ), pattern, &bd );
        if ( n == 0 ) return 0;
        // Convert UTF-8 → UTF-16 (incl. terminator) via the routine above.
        return MultiByteToWideChar( 0, 0, buf, -1, out, cchOut );
    }
}

int GetDateFormatW( LCID /*Locale*/, DWORD dwFlags, const SYSTEMTIME* lpDate,
                    LPCWSTR /*lpFormat*/, LPWSTR lpDateStr, int cchDate )
{
    const char* pattern;
    if ( dwFlags & DATE_LONGDATE )       pattern = "%A, %B %d, %Y";
    else if ( dwFlags & DATE_YEARMONTH ) pattern = "%Y-%m";
    else                                 pattern = "%m/%d/%Y";       // DATE_SHORTDATE / default
    return FormatViaStrftime( lpDate, pattern, lpDateStr, cchDate );
}

int GetTimeFormatW( LCID /*Locale*/, DWORD dwFlags, const SYSTEMTIME* lpTime,
                    LPCWSTR /*lpFormat*/, LPWSTR lpTimeStr, int cchTime )
{
    const char* pattern;
    if ( dwFlags & TIME_FORCE24HOURFORMAT )
    {
        if ( dwFlags & TIME_NOMINUTESORSECONDS ) pattern = "%H";
        else if ( dwFlags & TIME_NOSECONDS )     pattern = "%H:%M";
        else                                      pattern = "%H:%M:%S";
    }
    else
    {
        if ( dwFlags & TIME_NOMINUTESORSECONDS ) pattern = "%I %p";
        else if ( dwFlags & TIME_NOSECONDS )     pattern = "%I:%M %p";
        else                                      pattern = "%I:%M:%S %p";
    }
    return FormatViaStrftime( lpTime, pattern, lpTimeStr, cchTime );
}

}  // extern "C"
