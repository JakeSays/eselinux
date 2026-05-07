// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Internal: convert wide UTF-16 (16-bit WCHAR under -fshort-wchar) paths to
// narrow UTF-8 paths suitable for open()/stat()/etc. Centralized here so
// each File API entry point doesn't repeat the boilerplate.
//
// Engine path lengths are bounded by MAX_PATH (260) but Linux PATH_MAX is
// 4096 — we size the on-stack buffer to 4096+ for safety. WideToNarrow
// returns FALSE only when the path is empty or fails to convert.
#pragma once

#include "osstd.hxx"

#include <string.h>

namespace osposix
{

// Convert a null-terminated UTF-16 path to UTF-8 in caller-provided buffer.
// Returns the number of bytes written (excluding terminator) on success;
// 0 on failure. Caller should size buf to at least 4096 bytes.
inline int WidePathToUtf8( LPCWSTR wsz, char* buf, int cbBuf )
{
    if ( !wsz || !buf || cbBuf <= 0 )
    {
        return 0;
    }
    // WideCharToMultiByte with cchWideChar = -1 includes terminator in
    // the produced count; subtract 1 so callers see strlen-style length.
    const int n = WideCharToMultiByte( CP_UTF8, 0, wsz, -1,
                                       buf, cbBuf, nullptr, nullptr );
    if ( n <= 0 )
    {
        return 0;
    }
    return n - 1;
}

// Convert UTF-8 to UTF-16 in caller-provided WCHAR buffer (char count).
// Returns characters written (excluding terminator) on success, 0 otherwise.
inline int Utf8ToWide( const char* sz, LPWSTR wbuf, int cchBuf )
{
    if ( !sz || !wbuf || cchBuf <= 0 )
    {
        return 0;
    }
    const int n = MultiByteToWideChar( CP_UTF8, 0, sz, -1, wbuf, cchBuf );
    if ( n <= 0 )
    {
        return 0;
    }
    return n - 1;
}

}  // namespace osposix
