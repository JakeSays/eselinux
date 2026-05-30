// Common Win32-style character and string-pointer typedefs, shared by the
// windows-shim (winnt.h) and the published os.hxx-chain headers (types.hxx,
// string.hxx, error.hxx, ...).  Single source of truth so these are not
// duplicated per-header — several low-level TUs reach these headers without
// pulling winnt.h (no engine PCH), so the definitions must live somewhere the
// os.hxx chain itself includes.
//
// CHAR / LPSTR and the integral base types (LONG/ULONG/DWORD/...) live in
// cc.hxx; this header is only the character / string-pointer family.
#pragma once

typedef wchar_t             WCHAR;

typedef char*               PSTR;
typedef const char*         PCSTR;
typedef const char*         LPCSTR;
typedef WCHAR*              PWSTR;
typedef const WCHAR*        PCWSTR;
typedef WCHAR*              LPWSTR;
typedef const WCHAR*        LPCWSTR;
