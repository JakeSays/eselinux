// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 last-error code is per-thread. Use a `thread_local` DWORD;
// the engine reads/writes it through GetLastError/SetLastError.
//
// Bridging errno → Win32 codes is a separate concern handled at the
// boundary of each posix winapi_*.cxx — those translate as needed and
// SetLastError before returning failure.

#include "osstd.hxx"

namespace
{
    thread_local DWORD t_lastError = 0;
}

extern "C" {

DWORD GetLastError( void )
{
    return t_lastError;
}

void SetLastError( DWORD dwErrCode )
{
    t_lastError = dwErrCode;
}

}  // extern "C"
