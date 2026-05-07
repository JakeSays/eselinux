// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// FormatMessageW. On Windows this loads a localized string from a
// message-resource DLL keyed by message id. There is no Linux analogue
// — engine call sites pass FORMAT_MESSAGE_FROM_HMODULE, but the
// HMODULE points at the engine's own image, which has no embedded
// message table. We always return 0 (no message); the engine's call
// sites already handle that gracefully (the in-memory event ringbuffer
// just skips the event).

#include "osstd.hxx"

#include <stdarg.h>

extern "C" {

DWORD FormatMessageW( DWORD dwFlags, LPCVOID /*lpSource*/, DWORD /*dwMessageId*/,
                      DWORD /*dwLanguageId*/, LPWSTR lpBuffer, DWORD /*nSize*/,
                      va_list* /*Arguments*/ )
{
    if ( dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER )
    {
        // Caller will pass &(LPWSTR) and treat it as out-pointer.
        if ( lpBuffer ) *reinterpret_cast<LPWSTR*>( lpBuffer ) = nullptr;
    }
    return 0;
}

}  // extern "C"
