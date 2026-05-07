// Linux shim for <tchar.h>. Mirrors MSVC's behaviour: _TCHAR / _T() / the
// _tcs* family are wide when _UNICODE is defined, narrow otherwise. The
// project does NOT define _UNICODE globally — individual targets that
// want wide TCHAR opt in via target_compile_definitions(_UNICODE).
//
// Why match MSVC: the engine's OS layer (oswinnt on Windows) is built
// without _UNICODE, so call sites pass narrow string literals to APIs
// taking `const _TCHAR*` (e.g. `(*pcprintf)( "..." )`,
// `CSyncBasicInfo( _T("...") )`). An unconditionally-wide _TCHAR would
// break those sites. Other targets (devlibtest etc.) opt into wide.
#pragma once

#include <wchar.h>
#include <string.h>

#ifdef _UNICODE

#ifndef __TCHAR_DEFINED
#define __TCHAR_DEFINED
typedef wchar_t _TCHAR;
typedef wchar_t TCHAR;
#endif

#ifndef _T
#define _T(x)        L ## x
#endif
#ifndef _TEXT
#define _TEXT(x)     L ## x
#endif

#define _tcscpy      wcscpy
#define _tcsncpy     wcsncpy
#define _tcscat      wcscat
#define _tcslen      wcslen
#define _tcscmp      wcscmp
#define _tcsicmp     wcscasecmp
#define _tcsstr      wcsstr
#define _tcschr      wcschr
#define _tcsrchr     wcsrchr
#define _stprintf    swprintf
#define _vstprintf   vswprintf
#define _ftprintf    fwprintf
#define _tprintf     wprintf
#define _vtprintf    vwprintf
#define _vftprintf   vfwprintf

#else  // !_UNICODE

#ifndef __TCHAR_DEFINED
#define __TCHAR_DEFINED
typedef char    _TCHAR;
typedef char    TCHAR;
#endif

#ifndef _T
#define _T(x)        x
#endif
#ifndef _TEXT
#define _TEXT(x)     x
#endif

#define _tcscpy      strcpy
#define _tcsncpy     strncpy
#define _tcscat      strcat
#define _tcslen      strlen
#define _tcscmp      strcmp
#define _tcsicmp     strcasecmp
#define _tcsstr      strstr
#define _tcschr      strchr
#define _tcsrchr     strrchr
#define _stprintf    sprintf
#define _vstprintf   vsprintf
#define _ftprintf    fprintf
#define _tprintf     printf
#define _vtprintf    vprintf
#define _vftprintf   vfprintf

#endif  // _UNICODE
