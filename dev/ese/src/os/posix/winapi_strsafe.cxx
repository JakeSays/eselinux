// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// StringCb* / StringCch* family.  All the format-string heavy lifting
// lives in winapi_format.cxx (NarrowVPrintfImpl, WideVPrintfImpl); this
// file is just the bookkeeping wrappers around it plus the small Copy /
// Cat / Length primitives.
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

//  Implemented in winapi_format.cxx.
extern "C" HRESULT NarrowVPrintfImpl( char* dst, size_t cchDst, const char* fmt, va_list args );
extern "C" HRESULT WideVPrintfImpl( wchar_t* dst, size_t cchDst, const wchar_t* fmt, va_list args );

namespace
{

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

}  // namespace

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
