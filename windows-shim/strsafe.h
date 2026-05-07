// Linux shim for <strsafe.h>. Declares the StringCb*/StringCch* family
// the engine relies on. Implementations are provided in the os/posix
// layer (see osposix/strsafe.cxx — added when we wire up the OS layer).
//
// The Windows contract: each function returns an HRESULT (S_OK = 0 on
// success, S_FALSE / STRSAFE_E_INSUFFICIENT_BUFFER on truncation, etc.)
// and always leaves the destination NUL-terminated provided the buffer
// is non-empty.
//
// Wide variants take wchar_t* (which is 16-bit under -fshort-wchar) so
// they bind cleanly to engine WCHAR/_TCHAR call sites without casts.
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <wchar.h>

#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
// Match the engine's HRESULT (LONG = int32_t under cc.hxx). The Win32 SDK
// uses `long`, but on x86_64 Linux `long` is 64-bit and would split
// `ErrFromStrsafeHr(HRESULT)` into two manglings depending on which header
// is in the include chain.
typedef int HRESULT;
#endif

#ifndef S_OK
#define S_OK                                ((HRESULT)0L)
#endif
#ifndef S_FALSE
#define S_FALSE                             ((HRESULT)1L)
#endif

#define STRSAFE_E_INVALID_PARAMETER         ((HRESULT)0x80070057L)
#define STRSAFE_E_INSUFFICIENT_BUFFER       ((HRESULT)0x8007007AL)
#define STRSAFE_E_END_OF_FILE               ((HRESULT)0x80070026L)

// SEC_E_OK is sspi.h's success code; on Windows it's a synonym for S_OK
// (= 0). The engine's ErrFromStrsafeHr() checks for it as the StringSafe
// success value.
#ifndef SEC_E_OK
#define SEC_E_OK                            ((HRESULT)0L)
#endif

#ifdef __cplusplus
extern "C" {
#endif

HRESULT StringCbCopyA(char* dst, size_t cbDst, const char* src);
HRESULT StringCbCopyW(wchar_t* dst, size_t cbDst, const wchar_t* src);

HRESULT StringCbCatA(char* dst, size_t cbDst, const char* src);
HRESULT StringCbCatW(wchar_t* dst, size_t cbDst, const wchar_t* src);

HRESULT StringCbLengthA(const char* sz, size_t cbMax, size_t* pcb);
HRESULT StringCbLengthW(const wchar_t* wsz, size_t cbMax, size_t* pcb);

HRESULT StringCbPrintfA(char* dst, size_t cbDst, const char* fmt, ...);
HRESULT StringCbPrintfW(wchar_t* dst, size_t cbDst, const wchar_t* fmt, ...);

HRESULT StringCbVPrintfA(char* dst, size_t cbDst, const char* fmt, va_list args);
HRESULT StringCbVPrintfW(wchar_t* dst, size_t cbDst, const wchar_t* fmt, va_list args);

HRESULT StringCchCopyA(char* dst, size_t cchDst, const char* src);
HRESULT StringCchCopyW(wchar_t* dst, size_t cchDst, const wchar_t* src);

HRESULT StringCchCatA(char* dst, size_t cchDst, const char* src);
HRESULT StringCchCatW(wchar_t* dst, size_t cchDst, const wchar_t* src);

HRESULT StringCchLengthA(const char* sz, size_t cchMax, size_t* pcch);
HRESULT StringCchLengthW(const wchar_t* wsz, size_t cchMax, size_t* pcch);

HRESULT StringCchPrintfA(char* dst, size_t cchDst, const char* fmt, ...);
HRESULT StringCchPrintfW(wchar_t* dst, size_t cchDst, const wchar_t* fmt, ...);

HRESULT StringCchVPrintfA(char* dst, size_t cchDst, const char* fmt, va_list args);
HRESULT StringCchVPrintfW(wchar_t* dst, size_t cchDst, const wchar_t* fmt, va_list args);

#ifdef __cplusplus
} // extern "C"
#endif

// TCHAR-conditional aliases — the Linux port is UNICODE-only, so they
// always resolve to the wide variant.
#if defined(UNICODE) || defined(_UNICODE)
#define StringCbCopy        StringCbCopyW
#define StringCbCat         StringCbCatW
#define StringCbLength      StringCbLengthW
#define StringCbPrintf      StringCbPrintfW
#define StringCbVPrintf     StringCbVPrintfW
#define StringCchCopy       StringCchCopyW
#define StringCchCat        StringCchCatW
#define StringCchLength     StringCchLengthW
#define StringCchPrintf     StringCchPrintfW
#define StringCchVPrintf    StringCchVPrintfW
#else
#define StringCbCopy        StringCbCopyA
#define StringCbCat         StringCbCatA
#define StringCbLength      StringCbLengthA
#define StringCbPrintf      StringCbPrintfA
#define StringCbVPrintf     StringCbVPrintfA
#define StringCchCopy       StringCchCopyA
#define StringCchCat        StringCchCatA
#define StringCchLength     StringCchLengthA
#define StringCchPrintf     StringCchPrintfA
#define StringCchVPrintf    StringCchVPrintfA
#endif
