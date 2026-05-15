// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// MultiByteToWideChar / WideCharToMultiByte for the Linux port.
//
// Engine call sites hand us three codepages:
//   * CP_ACP        — the OS ANSI codepage.  On Linux there's no
//                     such concept, so we pin it to Windows-1252
//                     (en-US default).  Anyone wanting different
//                     behavior should use the W APIs.
//   * CP_THREAD_ACP — Windows uses the calling thread's locale CP;
//                     we treat it identically to CP_ACP.
//   * CP_UTF8       — proper UTF-8.  Used by the Linux-specific
//                     paths (syslog/journald, path strings, …).
// Anything else fails with ERROR_INVALID_PARAMETER.
//
// WCHAR is 16-bit (UTF-16) under -fshort-wchar.  We hand-roll the
// conversion rather than dragging in iconv: the codepoint range is
// what matters and both target encodings are fixed.
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
    //  Windows-1252 → Unicode mapping for 0x80..0x9F.  Bytes outside
    //  this range have the trivial identity mapping (0x00..0x7F is
    //  ASCII; 0xA0..0xFF is Latin-1 Supplement).  Five positions
    //  (0x81, 0x8D, 0x8F, 0x90, 0x9D) are unassigned in 1252 and
    //  pass through as the C1 control codepoint of the same value
    //  (matches Windows MultiByteToWideChar's documented behavior).
    constexpr uint16_t Cp1252To16_80_9F[ 32 ] =
    {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80..0x87
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88..0x8F
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90..0x97
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98..0x9F
    };

    bool IsAnsiCodePage( UINT cp )
    {
        //  CP_ACP / CP_OEMCP / CP_THREAD_ACP / CP_MACCP are all
        //  Win32-codepage placeholders; we route them all to 1252
        //  on Linux.  0 == CP_ACP literal.
        return cp == 0 || cp == CP_ACP || cp == CP_THREAD_ACP || cp == 1252;
    }

    //  Map a single 1252 byte to a Unicode codepoint.
    uint32_t Cp1252ToUnicode( unsigned char b )
    {
        if ( b < 0x80 || b >= 0xA0 )
        {
            return b;
        }
        return Cp1252To16_80_9F[ b - 0x80 ];
    }

    //  Map a Unicode codepoint to a 1252 byte, or 0 if not
    //  representable.  Caller decides whether 0 means "replace with
    //  default char" or "fail with ERROR_NO_UNICODE_TRANSLATION".
    int UnicodeToCp1252( uint32_t cp )
    {
        if ( cp <= 0x7F )
        {
            return static_cast<int>( cp );
        }
        if ( cp >= 0xA0 && cp <= 0xFF )
        {
            return static_cast<int>( cp );
        }
        //  Reverse-lookup the 0x80..0x9F range.  Small table; linear
        //  scan is fine.
        for ( int i = 0; i < 32; ++i )
        {
            if ( Cp1252To16_80_9F[ i ] == cp )
            {
                return 0x80 + i;
            }
        }
        return -1;
    }

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

namespace
{
    //  Single-step decoder: pull one codepoint from `[p, end)` under
    //  the named codepage, advance `p`.  Sets `*outValid = false` on
    //  malformed input (used by MB_ERR_INVALID_CHARS).
    uint32_t DecodeOne( UINT codepage,
                       const unsigned char*& p,
                       const unsigned char* end,
                       bool* outValid )
    {
        if ( codepage == CP_UTF8 )
        {
            const auto* const before = p;
            const uint32_t cp = DecodeUtf8( p, end );
            if ( cp == 0xFFFD )
            {
                //  Distinguish a real U+FFFD (encoded as EF BF BD)
                //  from a substituted-on-malformed U+FFFD by checking
                //  the consumed byte run.
                const ptrdiff_t consumed = p - before;
                if ( !( consumed == 3 && before[ 0 ] == 0xEF &&
                        before[ 1 ] == 0xBF && before[ 2 ] == 0xBD ) )
                {
                    *outValid = false;
                }
            }
            return cp;
        }
        //  ANSI / 1252: one byte → one codepoint, always valid.
        return Cp1252ToUnicode( *p++ );
    }
}

extern "C" {

int MultiByteToWideChar( UINT CodePage, DWORD dwFlags,
                         LPCSTR lpMultiByteStr, int cbMultiByte,
                         LPWSTR lpWideCharStr, int cchWideChar )
{
    UINT effective = CodePage;
    if ( IsAnsiCodePage( effective ) )
    {
        effective = 1252;
    }
    else if ( effective != CP_UTF8 )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }

    if ( !lpMultiByteStr )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }

    const bool errOnInvalid = ( dwFlags & MB_ERR_INVALID_CHARS ) != 0;

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
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }
    else
    {
        cb = static_cast<size_t>( cbMultiByte );
    }

    const auto* const begin = reinterpret_cast<const unsigned char*>( lpMultiByteStr );
    const auto* const end   = begin + cb;
    const auto*       p     = begin;
    bool valid = true;

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
            cp = DecodeOne( effective, p, end, &valid );
            if ( !valid && errOnInvalid )
            {
                SetLastError( ERROR_NO_UNICODE_TRANSLATION );
                return 0;
            }
        }

        WCHAR* dst = nullptr;
        if ( cchWideChar > 0 )
        {
            if ( produced >= cchWideChar )
            {
                SetLastError( ERROR_INSUFFICIENT_BUFFER );
                return 0;
            }
            dst = lpWideCharStr + produced;
        }
        const int wrote = EncodeUtf16( cp, dst );
        produced += wrote;

        if ( cp == 0 && nullTerminated ) break;
    }
    return produced;
}

int WideCharToMultiByte( UINT CodePage, DWORD dwFlags,
                         LPCWSTR lpWideCharStr, int cchWideChar,
                         LPSTR lpMultiByteStr, int cbMultiByte,
                         LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar )
{
    if ( lpUsedDefaultChar ) *lpUsedDefaultChar = FALSE;

    UINT effective = CodePage;
    if ( IsAnsiCodePage( effective ) )
    {
        effective = 1252;
    }
    else if ( effective != CP_UTF8 )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }

    if ( !lpWideCharStr )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }

    const bool errOnInvalid = ( dwFlags & WC_ERR_INVALID_CHARS ) != 0;

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
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }
    else
    {
        cch = static_cast<size_t>( cchWideChar );
    }

    const WCHAR* const begin = lpWideCharStr;
    const WCHAR* const end   = begin + cch;
    const WCHAR*       p     = begin;

    //  Default-char fallback for codepoints not representable in
    //  the target codepage.  CP_UTF8 never needs this; CP_1252 may
    //  need it for codepoints outside Latin-1 + the 27 1252 extras.
    const char defaultChar = ( lpDefaultChar != nullptr && lpDefaultChar[ 0 ] != '\0' )
                             ? lpDefaultChar[ 0 ]
                             : '?';

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
        int wrote;
        if ( effective == CP_UTF8 )
        {
            wrote = EncodeUtf8( cp, buf );
        }
        else
        {
            //  CP_1252 path.
            const int b = UnicodeToCp1252( cp );
            if ( b < 0 )
            {
                if ( errOnInvalid )
                {
                    SetLastError( ERROR_NO_UNICODE_TRANSLATION );
                    return 0;
                }
                if ( lpUsedDefaultChar ) *lpUsedDefaultChar = TRUE;
                buf[ 0 ] = defaultChar;
            }
            else
            {
                buf[ 0 ] = static_cast<char>( b );
            }
            wrote = 1;
        }
        if ( cbMultiByte > 0 )
        {
            if ( produced + wrote > cbMultiByte )
            {
                SetLastError( ERROR_INSUFFICIENT_BUFFER );
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
    //  en-US locale tables.  Phase 1 ships en-US only (matches the
    //  single-locale `g_rgEseMsgTable` and CP_ACP=1252 install
    //  posture).  Honoring other LCIDs would require libnls coverage
    //  for LOCALE_SDAYNAME* / LOCALE_SMONTHNAME* / LOCALE_S1159 /
    //  LOCALE_S2359 which it does not currently provide.
    const wchar_t* const EnUsDayFull[ 7 ] =
    {
        L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
        L"Thursday", L"Friday", L"Saturday"
    };
    const wchar_t* const EnUsDayAbbr[ 7 ] =
    {
        L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"
    };
    const wchar_t* const EnUsMonthFull[ 12 ] =
    {
        L"January", L"February", L"March",     L"April",
        L"May",     L"June",     L"July",      L"August",
        L"September", L"October", L"November", L"December"
    };
    const wchar_t* const EnUsMonthAbbr[ 12 ] =
    {
        L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
        L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"
    };

    void AppendWide( LPWSTR out, int cap, int& used, const wchar_t* s )
    {
        while ( *s != L'\0' )
        {
            if ( cap > 0 && used + 1 >= cap )
            {
                used = -1;
                return;
            }
            if ( out != nullptr && cap > 0 )
            {
                out[ used ] = *s;
            }
            ++used;
            ++s;
        }
    }

    void AppendWChar( LPWSTR out, int cap, int& used, wchar_t c )
    {
        if ( cap > 0 && used + 1 >= cap )
        {
            used = -1;
            return;
        }
        if ( out != nullptr && cap > 0 )
        {
            out[ used ] = c;
        }
        ++used;
    }

    void AppendIntPadded( LPWSTR out, int cap, int& used,
                          int value, int width )
    {
        wchar_t buf[ 8 ];
        int n = 0;
        if ( value == 0 )
        {
            buf[ n++ ] = L'0';
        }
        else
        {
            int v = value;
            while ( v > 0 && n < 8 )
            {
                buf[ n++ ] = static_cast<wchar_t>( L'0' + ( v % 10 ) );
                v /= 10;
            }
        }
        for ( int i = n; i < width; ++i )
        {
            AppendWChar( out, cap, used, L'0' );
        }
        while ( n > 0 )
        {
            AppendWChar( out, cap, used, buf[ --n ] );
        }
    }

    //  Count the run of identical letters starting at p.  Caller
    //  uses this to disambiguate %d / %dd / %ddd / %dddd etc.
    int RunLength( const wchar_t* p )
    {
        int n = 1;
        const wchar_t c = *p;
        while ( p[ n ] == c )
        {
            ++n;
        }
        return n;
    }

    //  Expand a Win32 date/time format string into `out`.  Returns
    //  the number of WCHARs written (excluding terminator), or 0
    //  on overflow / invalid input.  Tokens implemented:
    //    Date:   d dd ddd dddd  M MM MMM MMMM  y yy yyyy  gg
    //    Time:   h hh H HH  m mm  s ss  t tt
    //    Quoted: 'literal'  ('' = single quote)
    int ExpandFormat( const wchar_t* fmt, const SYSTEMTIME* st,
                      bool dateContext, LPWSTR out, int cap )
    {
        int used = 0;
        const int hour12 = ( st->wHour == 0 ) ? 12
                         : ( st->wHour > 12 ) ? st->wHour - 12
                                              : st->wHour;
        const bool pm = ( st->wHour >= 12 );

        for ( const wchar_t* p = fmt; *p != L'\0'; )
        {
            if ( *p == L'\'' )
            {
                ++p;
                while ( *p != L'\0' )
                {
                    if ( *p == L'\'' )
                    {
                        if ( p[ 1 ] == L'\'' )
                        {
                            AppendWChar( out, cap, used, L'\'' );
                            p += 2;
                            continue;
                        }
                        ++p;
                        break;
                    }
                    AppendWChar( out, cap, used, *p++ );
                    if ( used < 0 ) return 0;
                }
                if ( used < 0 ) return 0;
                continue;
            }

            if ( dateContext && *p == L'd' )
            {
                const int n = RunLength( p );
                p += n;
                if ( n == 1 )      AppendIntPadded( out, cap, used, st->wDay, 1 );
                else if ( n == 2 ) AppendIntPadded( out, cap, used, st->wDay, 2 );
                else if ( n == 3 ) AppendWide( out, cap, used, EnUsDayAbbr[ st->wDayOfWeek % 7 ] );
                else               AppendWide( out, cap, used, EnUsDayFull[ st->wDayOfWeek % 7 ] );
            }
            else if ( dateContext && *p == L'M' )
            {
                const int n = RunLength( p );
                p += n;
                const int month = ( st->wMonth >= 1 && st->wMonth <= 12 )
                                  ? st->wMonth
                                  : 1;
                if ( n == 1 )      AppendIntPadded( out, cap, used, month, 1 );
                else if ( n == 2 ) AppendIntPadded( out, cap, used, month, 2 );
                else if ( n == 3 ) AppendWide( out, cap, used, EnUsMonthAbbr[ month - 1 ] );
                else               AppendWide( out, cap, used, EnUsMonthFull[ month - 1 ] );
            }
            else if ( dateContext && *p == L'y' )
            {
                const int n = RunLength( p );
                p += n;
                if ( n == 1 )      AppendIntPadded( out, cap, used, st->wYear % 10, 1 );
                else if ( n == 2 ) AppendIntPadded( out, cap, used, st->wYear % 100, 2 );
                else               AppendIntPadded( out, cap, used, st->wYear, 4 );
            }
            else if ( dateContext && *p == L'g' )
            {
                //  Era marker — engine never persists this; emit "AD".
                const int n = RunLength( p );
                p += n;
                AppendWide( out, cap, used, L"AD" );
            }
            else if ( !dateContext && *p == L'h' )
            {
                const int n = RunLength( p );
                p += n;
                AppendIntPadded( out, cap, used, hour12, ( n >= 2 ) ? 2 : 1 );
            }
            else if ( !dateContext && *p == L'H' )
            {
                const int n = RunLength( p );
                p += n;
                AppendIntPadded( out, cap, used, st->wHour, ( n >= 2 ) ? 2 : 1 );
            }
            else if ( !dateContext && *p == L'm' )
            {
                const int n = RunLength( p );
                p += n;
                AppendIntPadded( out, cap, used, st->wMinute, ( n >= 2 ) ? 2 : 1 );
            }
            else if ( !dateContext && *p == L's' )
            {
                const int n = RunLength( p );
                p += n;
                AppendIntPadded( out, cap, used, st->wSecond, ( n >= 2 ) ? 2 : 1 );
            }
            else if ( !dateContext && *p == L't' )
            {
                const int n = RunLength( p );
                p += n;
                const wchar_t* marker = pm ? L"PM" : L"AM";
                if ( n == 1 )
                {
                    AppendWChar( out, cap, used, marker[ 0 ] );
                }
                else
                {
                    AppendWide( out, cap, used, marker );
                }
            }
            else
            {
                AppendWChar( out, cap, used, *p++ );
            }

            if ( used < 0 ) return 0;
        }

        //  Null-terminate.
        if ( cap > 0 && out != nullptr )
        {
            if ( used < cap )
            {
                out[ used ] = L'\0';
            }
            else
            {
                out[ cap - 1 ] = L'\0';
                return 0;  // overflow
            }
        }
        return used + 1;  // include terminator in returned count
    }

    //  Default Win32 date format strings on an en-US install.
    const wchar_t* DefaultDateFormat( DWORD dwFlags )
    {
        if ( dwFlags & DATE_LONGDATE )   return L"dddd, MMMM d, yyyy";
        if ( dwFlags & DATE_YEARMONTH )  return L"MMMM, yyyy";
        return L"M/d/yyyy";  // DATE_SHORTDATE / default
    }

    //  Default Win32 time format on en-US.
    const wchar_t* DefaultTimeFormat( DWORD dwFlags )
    {
        if ( dwFlags & TIME_FORCE24HOURFORMAT )
        {
            if ( dwFlags & TIME_NOMINUTESORSECONDS ) return L"HH";
            if ( dwFlags & TIME_NOSECONDS )           return L"HH:mm";
            return L"HH:mm:ss";
        }
        if ( dwFlags & TIME_NOMINUTESORSECONDS ) return L"h tt";
        if ( dwFlags & TIME_NOSECONDS )           return L"h:mm tt";
        return L"h:mm:ss tt";
    }
}

int GetDateFormatW( LCID /*Locale*/, DWORD dwFlags, const SYSTEMTIME* lpDate,
                    LPCWSTR lpFormat, LPWSTR lpDateStr, int cchDate )
{
    if ( lpDate == nullptr )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }
    const wchar_t* fmt = ( lpFormat != nullptr ) ? lpFormat
                                                 : DefaultDateFormat( dwFlags );
    return ExpandFormat( fmt, lpDate, /*dateContext=*/true, lpDateStr, cchDate );
}

int GetTimeFormatW( LCID /*Locale*/, DWORD dwFlags, const SYSTEMTIME* lpTime,
                    LPCWSTR lpFormat, LPWSTR lpTimeStr, int cchTime )
{
    if ( lpTime == nullptr )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }
    const wchar_t* fmt = ( lpFormat != nullptr ) ? lpFormat
                                                 : DefaultTimeFormat( dwFlags );
    return ExpandFormat( fmt, lpTime, /*dateContext=*/false, lpTimeStr, cchTime );
}

}  // extern "C"
