// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// StringCb* / StringCch* family. The narrow (A) variants delegate to the
// libc printf path. The wide (W) variants need a hand-rolled formatter
// because:
//
//   1. Engine wchar_t is 16-bit (-fshort-wchar); libc's vswprintf
//      operates on 32-bit native wchar_t and we'd have to reformat
//      every arg.
//   2. Windows format specifiers (%ws, %S, %I64d) differ from libc's.
//
// We walk the wide format string ourselves, format each conversion via
// libc snprintf into a small narrow staging buffer, and copy code points
// out as 16-bit WCHARs. Wide string arguments (%ws / %S / %ls) are
// emitted directly. Subset of supported specs is wide enough for engine
// uses surveyed in dev/ese/src/os/.
//
// Win32 semantics:
//   - "Cb" variants count BYTES; "Cch" variants count WCHARs/CHARs.
//   - On overflow, return STRSAFE_E_INSUFFICIENT_BUFFER but still leave
//     the buffer NUL-terminated (and truncated).
//   - cbDst==0 / cchDst==0 is invalid → STRSAFE_E_INVALID_PARAMETER.

#include "osstd.hxx"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace
{
    // ---- narrow helpers ---------------------------------------------------

    HRESULT NarrowCopyImpl( char* dst, size_t cchDst, const char* src )
    {
        if ( !dst || cchDst == 0 ) return STRSAFE_E_INVALID_PARAMETER;
        if ( !src ) { dst[ 0 ] = '\0'; return STRSAFE_E_INVALID_PARAMETER; }

        size_t i = 0;
        while ( i + 1 < cchDst && src[ i ] )
        {
            dst[ i ] = src[ i ];
            ++i;
        }
        dst[ i ] = '\0';
        return src[ i ] ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
    }

    HRESULT NarrowCatImpl( char* dst, size_t cchDst, const char* src )
    {
        if ( !dst || cchDst == 0 ) return STRSAFE_E_INVALID_PARAMETER;
        if ( !src ) return STRSAFE_E_INVALID_PARAMETER;

        size_t i = 0;
        while ( i < cchDst && dst[ i ] ) ++i;
        if ( i == cchDst ) { return STRSAFE_E_INVALID_PARAMETER; }  // unterminated
        return NarrowCopyImpl( dst + i, cchDst - i, src );
    }

    HRESULT NarrowVPrintfImpl( char* dst, size_t cchDst, const char* fmt, va_list args )
    {
        if ( !dst || cchDst == 0 ) return STRSAFE_E_INVALID_PARAMETER;
        const int n = vsnprintf( dst, cchDst, fmt, args );
        if ( n < 0 )
        {
            dst[ 0 ] = '\0';
            return STRSAFE_E_INVALID_PARAMETER;
        }
        if ( static_cast<size_t>( n ) >= cchDst )
        {
            return STRSAFE_E_INSUFFICIENT_BUFFER;  // already nul-terminated by vsnprintf
        }
        return S_OK;
    }

    // ---- wide formatter ---------------------------------------------------
    //
    // Emit one WCHAR to *dst (advancing it) if we're under cap. Return
    // false once we've truncated (caller terminates and reports).

    struct WSink
    {
        wchar_t* dst;
        size_t   cap;     // total slots including terminator
        size_t   used;    // wchars written so far (excl. terminator)
        bool     overflow;

        void Put( wchar_t c )
        {
            if ( used + 1 < cap )
            {
                dst[ used++ ] = c;
            }
            else
            {
                overflow = true;
            }
        }

        void PutAscii( const char* s, size_t n )
        {
            for ( size_t i = 0; i < n; ++i ) Put( static_cast<wchar_t>( s[ i ] ) );
        }
    };

    // Parse one conversion spec starting at *fmt (which is on the char
    // after '%'). Returns the conversion character, advances *fmt past it,
    // and copies the verbatim spec into out (e.g. "08x"). We rebuild the
    // libc-equivalent spec for numeric formatting.
    struct SpecInfo
    {
        char  flags[ 8 ];      size_t flagsN = 0;
        char  width[ 16 ];     size_t widthN = 0;
        char  prec[ 16 ];      size_t precN  = 0;
        bool  hasPrec = false;
        // Size: 0=int, 'h'=short, 'l'=long, 'L'='ll', 'z'=size_t, 't'=ptrdiff_t, '6'=int64
        char  size = 0;
        bool  wide = false;    // %ws %wc %S %lS — wide string/char form
        char  conv = 0;
    };

    void ParseSpec( const wchar_t*& p, SpecInfo* s )
    {
        // flags
        for ( ;; )
        {
            const wchar_t c = *p;
            if ( c == L'-' || c == L'+' || c == L' ' || c == L'#' || c == L'0' )
            {
                if ( s->flagsN < sizeof( s->flags ) ) s->flags[ s->flagsN++ ] = static_cast<char>( c );
                ++p;
            }
            else break;
        }
        // width
        while ( *p >= L'0' && *p <= L'9' )
        {
            if ( s->widthN < sizeof( s->width ) ) s->width[ s->widthN++ ] = static_cast<char>( *p );
            ++p;
        }
        // precision
        if ( *p == L'.' )
        {
            s->hasPrec = true;
            ++p;
            while ( *p >= L'0' && *p <= L'9' )
            {
                if ( s->precN < sizeof( s->prec ) ) s->prec[ s->precN++ ] = static_cast<char>( *p );
                ++p;
            }
        }
        // size modifiers
        if ( *p == L'I' )
        {
            ++p;
            if ( p[ 0 ] == L'6' && p[ 1 ] == L'4' ) { s->size = '6'; p += 2; }
            else if ( p[ 0 ] == L'3' && p[ 1 ] == L'2' ) { s->size = 0; p += 2; }
            else { s->size = 'z'; }  // %Ix — pointer-sized
        }
        else if ( *p == L'l' )
        {
            ++p;
            if ( *p == L'l' ) { s->size = 'L'; ++p; }
            else { s->size = 'l'; }
        }
        else if ( *p == L'h' )
        {
            ++p;
            s->size = 'h';
            if ( *p == L'h' ) ++p;  // hh — collapse to short for v1
        }
        else if ( *p == L'z' ) { s->size = 'z'; ++p; }
        else if ( *p == L't' ) { s->size = 't'; ++p; }
        else if ( *p == L'L' ) { s->size = 'L'; ++p; }
        else if ( *p == L'w' ) { s->wide = true; ++p; }

        s->conv = static_cast<char>( *p );
        if ( *p ) ++p;
    }

    // Build a libc-compatible narrow format spec like "%08lx" for numeric
    // conversion. Returns # bytes written into out (excluding terminator).
    size_t BuildNarrowSpec( const SpecInfo& s, char convOverride, char* out )
    {
        size_t n = 0;
        out[ n++ ] = '%';
        for ( size_t i = 0; i < s.flagsN; ++i ) out[ n++ ] = s.flags[ i ];
        for ( size_t i = 0; i < s.widthN; ++i ) out[ n++ ] = s.width[ i ];
        if ( s.hasPrec )
        {
            out[ n++ ] = '.';
            for ( size_t i = 0; i < s.precN; ++i ) out[ n++ ] = s.prec[ i ];
        }
        switch ( s.size )
        {
        case 'h': out[ n++ ] = 'h'; break;
        case 'l': out[ n++ ] = 'l'; break;
        case 'L': out[ n++ ] = 'l'; out[ n++ ] = 'l'; break;
        case 'z': out[ n++ ] = 'z'; break;
        case 't': out[ n++ ] = 't'; break;
        case '6': out[ n++ ] = 'l'; out[ n++ ] = 'l'; break;
        default: break;
        }
        out[ n++ ] = convOverride;
        out[ n ] = '\0';
        return n;
    }

    // Decode UTF-16 codepoint (in case of surrogate pairs) — for emitting
    // wide strings to the wide sink we just copy WCHARs through, no decode
    // needed; this helper is only used when sourcing from %s narrow input.

    void EmitNarrowString( WSink& sink, const SpecInfo& s, const char* p )
    {
        if ( !p ) p = "(null)";
        size_t srcLen = strlen( p );
        if ( s.hasPrec )
        {
            size_t prec = 0;
            for ( size_t i = 0; i < s.precN; ++i ) prec = prec * 10 + ( s.prec[ i ] - '0' );
            if ( srcLen > prec ) srcLen = prec;
        }
        size_t width = 0;
        for ( size_t i = 0; i < s.widthN; ++i ) width = width * 10 + ( s.width[ i ] - '0' );
        const bool leftAlign = ( memchr( s.flags, '-', s.flagsN ) != nullptr );
        const size_t pad = ( width > srcLen ) ? width - srcLen : 0;
        if ( !leftAlign ) for ( size_t i = 0; i < pad; ++i ) sink.Put( L' ' );
        for ( size_t i = 0; i < srcLen; ++i ) sink.Put( static_cast<wchar_t>( p[ i ] ) );
        if (  leftAlign ) for ( size_t i = 0; i < pad; ++i ) sink.Put( L' ' );
    }

    void EmitWideString( WSink& sink, const SpecInfo& s, const wchar_t* p )
    {
        if ( !p ) { EmitNarrowString( sink, s, "(null)" ); return; }
        size_t srcLen = 0;
        while ( p[ srcLen ] ) ++srcLen;
        if ( s.hasPrec )
        {
            size_t prec = 0;
            for ( size_t i = 0; i < s.precN; ++i ) prec = prec * 10 + ( s.prec[ i ] - '0' );
            if ( srcLen > prec ) srcLen = prec;
        }
        size_t width = 0;
        for ( size_t i = 0; i < s.widthN; ++i ) width = width * 10 + ( s.width[ i ] - '0' );
        const bool leftAlign = ( memchr( s.flags, '-', s.flagsN ) != nullptr );
        const size_t pad = ( width > srcLen ) ? width - srcLen : 0;
        if ( !leftAlign ) for ( size_t i = 0; i < pad; ++i ) sink.Put( L' ' );
        for ( size_t i = 0; i < srcLen; ++i ) sink.Put( p[ i ] );
        if (  leftAlign ) for ( size_t i = 0; i < pad; ++i ) sink.Put( L' ' );
    }

    HRESULT WideVPrintfImpl( wchar_t* dst, size_t cchDst, const wchar_t* fmt, va_list args )
    {
        if ( !dst || cchDst == 0 ) return STRSAFE_E_INVALID_PARAMETER;
        if ( !fmt ) { dst[ 0 ] = 0; return STRSAFE_E_INVALID_PARAMETER; }

        WSink sink{ dst, cchDst, 0, false };

        for ( const wchar_t* p = fmt; *p; )
        {
            if ( *p != L'%' )
            {
                sink.Put( *p++ );
                continue;
            }
            ++p;
            if ( *p == L'%' ) { sink.Put( L'%' ); ++p; continue; }

            SpecInfo s{};
            ParseSpec( p, &s );

            switch ( s.conv )
            {
            case 's':
            {
                if ( s.size == 'l' || s.size == 'L' || s.wide )
                {
                    EmitWideString( sink, s, va_arg( args, const wchar_t* ) );
                }
                else
                {
                    EmitNarrowString( sink, s, va_arg( args, const char* ) );
                }
                break;
            }
            case 'S':  // opposite-width string in Win convention
            {
                EmitWideString( sink, s, va_arg( args, const wchar_t* ) );
                break;
            }
            case 'c':
            {
                if ( s.size == 'l' || s.size == 'L' || s.wide )
                {
                    sink.Put( static_cast<wchar_t>( va_arg( args, int ) ) );
                }
                else
                {
                    sink.Put( static_cast<wchar_t>( static_cast<unsigned char>( va_arg( args, int ) ) ) );
                }
                break;
            }
            case 'C':
            {
                sink.Put( static_cast<wchar_t>( va_arg( args, int ) ) );
                break;
            }
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
            case 'p':
            case 'f': case 'e': case 'g': case 'E': case 'G': case 'a': case 'A':
            {
                char nspec[ 64 ];
                BuildNarrowSpec( s, s.conv, nspec );
                char buf[ 96 ];
                int n = 0;
                if ( s.conv == 'p' )
                {
                    n = snprintf( buf, sizeof( buf ), nspec, va_arg( args, void* ) );
                }
                else if ( s.conv == 'f' || s.conv == 'e' || s.conv == 'g'
                       || s.conv == 'E' || s.conv == 'G' || s.conv == 'a' || s.conv == 'A' )
                {
                    n = snprintf( buf, sizeof( buf ), nspec, va_arg( args, double ) );
                }
                else if ( s.size == '6' || s.size == 'L' )
                {
                    n = snprintf( buf, sizeof( buf ), nspec, va_arg( args, long long ) );
                }
                else if ( s.size == 'l' )
                {
                    n = snprintf( buf, sizeof( buf ), nspec, va_arg( args, long ) );
                }
                else if ( s.size == 'z' )
                {
                    n = snprintf( buf, sizeof( buf ), nspec, va_arg( args, size_t ) );
                }
                else if ( s.size == 't' )
                {
                    n = snprintf( buf, sizeof( buf ), nspec, va_arg( args, ptrdiff_t ) );
                }
                else
                {
                    n = snprintf( buf, sizeof( buf ), nspec, va_arg( args, int ) );
                }
                if ( n < 0 ) n = 0;
                if ( n > static_cast<int>( sizeof( buf ) - 1 ) ) n = static_cast<int>( sizeof( buf ) - 1 );
                sink.PutAscii( buf, static_cast<size_t>( n ) );
                break;
            }
            case 'n':
                // %n is a security hazard; engine doesn't use it. Skip arg.
                (void)va_arg( args, int* );
                break;
            default:
                // Unknown — emit verbatim so we don't silently drop debug output.
                sink.Put( L'%' );
                sink.Put( static_cast<wchar_t>( s.conv ) );
                break;
            }
        }

        sink.dst[ sink.used ] = 0;
        return sink.overflow ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
    }
}

extern "C" {

// ---- byte-count variants -------------------------------------------------

HRESULT StringCbCopyA( char* dst, size_t cbDst, const char* src )
{
    return NarrowCopyImpl( dst, cbDst, src );
}

HRESULT StringCbCopyW( wchar_t* dst, size_t cbDst, const wchar_t* src )
{
    if ( !dst || cbDst < sizeof( wchar_t ) ) return STRSAFE_E_INVALID_PARAMETER;
    if ( !src ) { dst[ 0 ] = 0; return STRSAFE_E_INVALID_PARAMETER; }
    const size_t cchDst = cbDst / sizeof( wchar_t );
    size_t i = 0;
    while ( i + 1 < cchDst && src[ i ] ) { dst[ i ] = src[ i ]; ++i; }
    dst[ i ] = 0;
    return src[ i ] ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}

HRESULT StringCbCatA( char* dst, size_t cbDst, const char* src )
{
    return NarrowCatImpl( dst, cbDst, src );
}

HRESULT StringCbCatW( wchar_t* dst, size_t cbDst, const wchar_t* src )
{
    if ( !dst || cbDst < sizeof( wchar_t ) ) return STRSAFE_E_INVALID_PARAMETER;
    if ( !src ) return STRSAFE_E_INVALID_PARAMETER;
    const size_t cchDst = cbDst / sizeof( wchar_t );
    size_t i = 0;
    while ( i < cchDst && dst[ i ] ) ++i;
    if ( i == cchDst ) return STRSAFE_E_INVALID_PARAMETER;
    return StringCbCopyW( dst + i, ( cchDst - i ) * sizeof( wchar_t ), src );
}

HRESULT StringCbLengthA( const char* sz, size_t cbMax, size_t* pcb )
{
    if ( !sz || cbMax == 0 ) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    while ( i < cbMax && sz[ i ] ) ++i;
    if ( i == cbMax ) return STRSAFE_E_INVALID_PARAMETER;
    if ( pcb ) *pcb = i;
    return S_OK;
}

HRESULT StringCbLengthW( const wchar_t* wsz, size_t cbMax, size_t* pcb )
{
    if ( !wsz || cbMax < sizeof( wchar_t ) ) return STRSAFE_E_INVALID_PARAMETER;
    const size_t cchMax = cbMax / sizeof( wchar_t );
    size_t i = 0;
    while ( i < cchMax && wsz[ i ] ) ++i;
    if ( i == cchMax ) return STRSAFE_E_INVALID_PARAMETER;
    if ( pcb ) *pcb = i * sizeof( wchar_t );
    return S_OK;
}

HRESULT StringCbVPrintfA( char* dst, size_t cbDst, const char* fmt, va_list args )
{
    return NarrowVPrintfImpl( dst, cbDst, fmt, args );
}

HRESULT StringCbVPrintfW( wchar_t* dst, size_t cbDst, const wchar_t* fmt, va_list args )
{
    return WideVPrintfImpl( dst, cbDst / sizeof( wchar_t ), fmt, args );
}

HRESULT StringCbPrintfA( char* dst, size_t cbDst, const char* fmt, ... )
{
    va_list a; va_start( a, fmt );
    const HRESULT hr = StringCbVPrintfA( dst, cbDst, fmt, a );
    va_end( a );
    return hr;
}

HRESULT StringCbPrintfW( wchar_t* dst, size_t cbDst, const wchar_t* fmt, ... )
{
    va_list a; va_start( a, fmt );
    const HRESULT hr = StringCbVPrintfW( dst, cbDst, fmt, a );
    va_end( a );
    return hr;
}

// ---- character-count variants --------------------------------------------

HRESULT StringCchCopyA( char* dst, size_t cchDst, const char* src )
{
    return NarrowCopyImpl( dst, cchDst, src );
}

HRESULT StringCchCopyW( wchar_t* dst, size_t cchDst, const wchar_t* src )
{
    return StringCbCopyW( dst, cchDst * sizeof( wchar_t ), src );
}

HRESULT StringCchCatA( char* dst, size_t cchDst, const char* src )
{
    return NarrowCatImpl( dst, cchDst, src );
}

HRESULT StringCchCatW( wchar_t* dst, size_t cchDst, const wchar_t* src )
{
    return StringCbCatW( dst, cchDst * sizeof( wchar_t ), src );
}

HRESULT StringCchLengthA( const char* sz, size_t cchMax, size_t* pcch )
{
    if ( !sz || cchMax == 0 ) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    while ( i < cchMax && sz[ i ] ) ++i;
    if ( i == cchMax ) return STRSAFE_E_INVALID_PARAMETER;
    if ( pcch ) *pcch = i;
    return S_OK;
}

HRESULT StringCchLengthW( const wchar_t* wsz, size_t cchMax, size_t* pcch )
{
    if ( !wsz || cchMax == 0 ) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    while ( i < cchMax && wsz[ i ] ) ++i;
    if ( i == cchMax ) return STRSAFE_E_INVALID_PARAMETER;
    if ( pcch ) *pcch = i;
    return S_OK;
}

HRESULT StringCchVPrintfA( char* dst, size_t cchDst, const char* fmt, va_list args )
{
    return NarrowVPrintfImpl( dst, cchDst, fmt, args );
}

HRESULT StringCchVPrintfW( wchar_t* dst, size_t cchDst, const wchar_t* fmt, va_list args )
{
    return WideVPrintfImpl( dst, cchDst, fmt, args );
}

HRESULT StringCchPrintfA( char* dst, size_t cchDst, const char* fmt, ... )
{
    va_list a; va_start( a, fmt );
    const HRESULT hr = StringCchVPrintfA( dst, cchDst, fmt, a );
    va_end( a );
    return hr;
}

HRESULT StringCchPrintfW( wchar_t* dst, size_t cchDst, const wchar_t* fmt, ... )
{
    va_list a; va_start( a, fmt );
    const HRESULT hr = StringCchVPrintfW( dst, cchDst, fmt, a );
    va_end( a );
    return hr;
}

}  // extern "C"
