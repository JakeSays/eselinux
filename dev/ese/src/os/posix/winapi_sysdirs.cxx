// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// GetSystemWindowsDirectoryW / GetWindowsDirectoryW / GetSystemDirectoryW
// Engine call sites use these to locate global resource files (e.g. the
// system messages DLL). On Linux the conventional location is a runtime
// data directory under /var/lib/ese or a build-time configured prefix;
// for v1 we hardcode /var/lib/ese — the caller does StringCchCat on a
// resource filename, so as long as the directory exists at runtime, the
// engine sees a valid path.
//
// The returned form is a UTF-16 (under -fshort-wchar, 16-bit wchar_t)
// string. We synthesize it byte-by-byte from the ASCII source.

#include "osstd.hxx"

#include <string.h>

namespace
{
    constexpr const char* c_systemDir = "/var/lib/ese";

    UINT FillWide( LPWSTR lpBuffer, UINT uSize )
    {
        const size_t cb = strlen( c_systemDir );
        if ( uSize == 0 )
        {
            return static_cast<UINT>( cb + 1 );  // required size incl. null
        }
        if ( static_cast<size_t>( uSize ) <= cb )
        {
            return static_cast<UINT>( cb + 1 );  // buffer too small; engine retries
        }
        for ( size_t i = 0; i < cb; ++i )
        {
            lpBuffer[ i ] = static_cast<WCHAR>( c_systemDir[ i ] );
        }
        lpBuffer[ cb ] = 0;
        return static_cast<UINT>( cb );
    }
}

extern "C" {

UINT GetSystemWindowsDirectoryW( LPWSTR lpBuffer, UINT uSize )
{
    return FillWide( lpBuffer, uSize );
}

UINT GetWindowsDirectoryW( LPWSTR lpBuffer, UINT uSize )
{
    return FillWide( lpBuffer, uSize );
}

UINT GetSystemDirectoryW( LPWSTR lpBuffer, UINT uSize )
{
    return FillWide( lpBuffer, uSize );
}

}  // extern "C"
