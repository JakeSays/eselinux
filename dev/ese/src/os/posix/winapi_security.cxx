// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 process-token / privilege APIs.  Linux has no token-based
// privilege model — these all fail with ERROR_NOT_SUPPORTED and the
// engine's call sites (osfs.cxx ErrSetPrivilege) treat the failure
// as "feature unavailable" and fall through to the non-privileged
// code path.
//
// The declarations live in windows-shim/windows.h so the engine's
// NTOSFuncStd / FunctionLoaderStaticShim machinery can take their
// addresses at link time.

#include "osstd.hxx"

extern "C"
{

BOOL OpenProcessToken(HANDLE /*ProcessHandle*/, DWORD /*DesiredAccess*/, PHANDLE TokenHandle)
{
    if (TokenHandle)
        *TokenHandle = nullptr;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL AdjustTokenPrivileges(HANDLE /*TokenHandle*/, BOOL /*DisableAllPrivileges*/,
    PTOKEN_PRIVILEGES /*NewState*/, DWORD /*BufferLength*/,
    PTOKEN_PRIVILEGES /*PreviousState*/, PDWORD ReturnLength)
{
    if (ReturnLength)
        *ReturnLength = 0;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL LookupPrivilegeValueW(LPCWSTR /*lpSystemName*/, LPCWSTR /*lpName*/, PLUID lpLuid)
{
    if (lpLuid)
    {
        lpLuid->LowPart = 0;
        lpLuid->HighPart = 0;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

} // extern "C"
