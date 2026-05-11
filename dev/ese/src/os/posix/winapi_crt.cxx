// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Microsoft CRT extensions that have no glibc equivalent (or whose glibc
// equivalent has the wrong width). Engine code uses _wcsicmp / _strupr
// freely; declarations live in cc.hxx.
//
// _wcsicmp operates on 16-bit WCHAR (we build with -fshort-wchar). Glibc
// only exposes wcscasecmp on 32-bit wchar_t, so we hand-roll the compare.

#include "osstd.hxx"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

// 16-bit wide-string helpers. Engine + eseutil + tests use the standard
// wcs* names (wcschr, wcslen, wcscmp, ...) under the assumption that
// wchar_t is the platform's "wide" type. Glibc's wcs* operate on the
// native 32-bit wchar_t; -fshort-wchar makes ours 16-bit, so glibc
// reads two characters per step and produces garbage. Override every
// wcs* the engine actually uses with a 16-bit-aware implementation.
// glibc declares wchar_t-fn pairs (const + non-const) in <wchar.h>; we
// undef them so our extern "C" definitions don't fight the declared
// signatures.
#undef wcslen
#undef wcscmp
#undef wcsncmp
#undef wcschr
#undef wcsrchr
#undef wcsstr
#undef wcscspn
#undef wcscpy
#undef wcsncpy
#undef wcscat

// Defined with C++ linkage to match libc++'s declared signatures. Headers
// like <wchar.h>/<cwchar> declare these in C++ namespace plus pair them
// with extern "C" — but our overrides have to interpret 16-bit wchar_t,
// so we just provide them under C++ linkage and let the linker pick ours
// over libc's by being earlier in the link order via osposix.a.

size_t wcslen(const wchar_t* s) noexcept
{
    const wchar_t* p = s;
    while (*p)
        ++p;
    return static_cast<size_t>(p - s);
}

int wcscmp(const wchar_t* a, const wchar_t* b) noexcept
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return static_cast<int>(static_cast<unsigned int>(*a)) -
        static_cast<int>(static_cast<unsigned int>(*b));
}

int wcsncmp(const wchar_t* a, const wchar_t* b, size_t n) noexcept
{
    while (n && *a && *a == *b)
    {
        ++a;
        ++b;
        --n;
    }
    if (n == 0)
        return 0;
    return static_cast<int>(static_cast<unsigned int>(*a)) -
        static_cast<int>(static_cast<unsigned int>(*b));
}

wchar_t* wcschr(wchar_t* s, wchar_t c) noexcept
{
    for (; *s; ++s)
        if (*s == c)
            return s;
    return c == 0
           ? s
           : nullptr;
}

wchar_t* wcsrchr(wchar_t* s, wchar_t c) noexcept
{
    wchar_t* last = nullptr;
    for (; *s; ++s)
        if (*s == c)
            last = s;
    if (c == 0)
        return s;
    return last;
}

wchar_t* wcsstr(wchar_t* hay, const wchar_t* needle) noexcept
{
    if (!*needle)
        return hay;
    for (; *hay; ++hay)
    {
        wchar_t* h = hay;
        const wchar_t* n = needle;
        while (*h && *n && *h == *n)
        {
            ++h;
            ++n;
        }
        if (!*n)
            return hay;
    }
    return nullptr;
}

size_t wcscspn(const wchar_t* s, const wchar_t* reject) noexcept
{
    size_t n = 0;
    while (s[n])
    {
        for (const wchar_t* r = reject; *r; ++r)
        {
            if (s[n] == *r)
                return n;
        }
        ++n;
    }
    return n;
}

wchar_t* wcscpy(wchar_t* dst, const wchar_t* src) noexcept
{
    wchar_t* p = dst;
    while ((*p++ = *src++))
    {
    }
    return dst;
}

wchar_t* wcsncpy(wchar_t* dst, const wchar_t* src, size_t n) noexcept
{
    size_t i = 0;
    for (; i < n && src[i]; ++i)
        dst[i] = src[i];
    for (; i < n; ++i)
        dst[i] = 0;
    return dst;
}

wchar_t* wcscat(wchar_t* dst, const wchar_t* src) noexcept
{
    wchar_t* p = dst;
    while (*p)
        ++p;
    while ((*p++ = *src++))
    {
    }
    return dst;
}

extern "C"
{
int _wcsicmp(const wchar_t* s1, const wchar_t* s2)
{
    while (*s1 && *s2)
    {
        const int c1 = (*s1 < 0x80)
                       ? tolower(*s1)
                       : *s1;
        const int c2 = (*s2 < 0x80)
                       ? tolower(*s2)
                       : *s2;
        if (c1 != c2)
            return c1 - c2;
        ++s1;
        ++s2;
    }
    const int c1 = (*s1 < 0x80)
                   ? tolower(*s1)
                   : *s1;
    const int c2 = (*s2 < 0x80)
                   ? tolower(*s2)
                   : *s2;
    return c1 - c2;
}

int _wcsnicmp(const wchar_t* s1, const wchar_t* s2, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const int c1 = (s1[i] < 0x80)
                       ? tolower(s1[i])
                       : s1[i];
        const int c2 = (s2[i] < 0x80)
                       ? tolower(s2[i])
                       : s2[i];
        if (c1 != c2)
            return c1 - c2;
        if (c1 == 0)
            return 0;
    }
    return 0;
}

int _strnicmp(const char* s1, const char* s2, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const int c1 = tolower(static_cast<unsigned char>(s1[i]));
        const int c2 = tolower(static_cast<unsigned char>(s2[i]));
        if (c1 != c2)
            return c1 - c2;
        if (c1 == 0)
            return 0;
    }
    return 0;
}

// Wide-string to LONG. Skips leading ASCII whitespace, accepts an optional
// '+' / '-', then consumes ASCII digits. Saturates on overflow like the MSVC
// CRT (returns LONG_MAX / LONG_MIN). Width is fixed at LONG (int32_t in our
// cc.hxx) — the engine treats this as a Windows-style 32-bit `long`.
LONG _wtol(const wchar_t* s)
{
    if (!s)
        return 0;
    while (*s == L' ' || *s == L'\t' || *s == L'\n' || *s == L'\r')
        ++s;
    int sign = 1;
    if (*s == L'-')
    {
        sign = -1;
        ++s;
    }
    else
        if (*s == L'+') { ++s; }
    long long acc = 0;
    while (*s >= L'0' && *s <= L'9')
    {
        acc = acc * 10 + (*s - L'0');
        if (acc > 0x7fffffffLL)
            return sign > 0
                   ? 0x7fffffffL
                   : -0x7fffffffL - 1;
        ++s;
    }
    acc *= sign;
    return static_cast<LONG>(acc);
}

int _wtoi(const wchar_t* s)
{
    return static_cast<int>(_wtol(s));
}

// Wide-string to (unsigned) 64-bit. Engine usage passes radix 10 or 16;
// no octal/auto-base support needed. Mirrors MSVC: stop at first non-digit,
// optionally yield a tail pointer through pwszEnd.
static int DigitValueAscii(wchar_t c, int radix)
{
    int v = -1;
    if (c >= L'0' && c <= L'9')
        v = c - L'0';
    else if (c >= L'a' && c <= L'z')
        v = c - L'a' + 10;
    else
        if (c >= L'A' && c <= L'Z')
            v = c - L'A' + 10;
    return (v >= 0 && v < radix)
           ? v
           : -1;
}

unsigned long long _wcstoui64(const wchar_t* wsz, wchar_t** pwszEnd, int radix)
{
    if (!wsz)
    {
        if (pwszEnd)
            *pwszEnd = nullptr;
        return 0;
    }
    const wchar_t* p = wsz;
    while (*p == L' ' || *p == L'\t' || *p == L'\n' || *p == L'\r')
        ++p;
    if (*p == L'+')
        ++p;
    if (radix == 0)
        radix = 10;
    if (radix == 16 && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X'))
        p += 2;
    unsigned long long acc = 0;
    int d;
    while ((d = DigitValueAscii(*p, radix)) >= 0)
    {
        acc = acc * radix + d;
        ++p;
    }
    if (pwszEnd)
        *pwszEnd = const_cast<wchar_t*>(p);
    return acc;
}

long long _wcstoi64(const wchar_t* wsz, wchar_t** pwszEnd, int radix)
{
    if (!wsz)
    {
        if (pwszEnd)
            *pwszEnd = nullptr;
        return 0;
    }
    const wchar_t* p = wsz;
    while (*p == L' ' || *p == L'\t' || *p == L'\n' || *p == L'\r')
        ++p;
    int sign = 1;
    if (*p == L'-')
    {
        sign = -1;
        ++p;
    }
    else
        if (*p == L'+')
            ++p;
    unsigned long long mag = _wcstoui64(p, pwszEnd, radix);
    return sign < 0
           ? -static_cast<long long>(mag)
           : static_cast<long long>(mag);
}

// Narrow variants. Used by devlibtest's iterquery suite. Implementation
// matches glibc's strtoull semantics for the radixes the engine cares
// about (10, 16, 0=auto-detect via 0x prefix).
unsigned long long _strtoui64(const char* sz, char** pszEnd, int radix)
{
    if (!sz)
    {
        if (pszEnd)
            *pszEnd = nullptr;
        return 0;
    }
    return strtoull(sz, pszEnd, radix);
}

long long _strtoi64(const char* sz, char** pszEnd, int radix)
{
    if (!sz)
    {
        if (pszEnd)
            *pszEnd = nullptr;
        return 0;
    }
    return strtoll(sz, pszEnd, radix);
}

// Helper: copy ASCII-only 16-bit wide string into a narrow buffer.
// Returns false if the input contains a non-ASCII codepoint or overflows.
static bool WideToNarrowAscii(const wchar_t* src, char* dst, size_t cch)
{
    size_t i = 0;
    while (i + 1 < cch && src[i] != 0)
    {
        if (static_cast<unsigned>(src[i]) > 0x7f)
            return false;
        dst[i] = static_cast<char>(src[i]);
        ++i;
    }
    if (src[i] != 0)
        return false;
    dst[i] = 0;
    return true;
}

//  Implemented in winapi_format.cxx — handles MSVC %I64/%I32 and the
//  LP64 lone-%l footgun directly, so we never hand a Win-flavoured
//  format string to libc's vsscanf.
extern "C" int NarrowVSScanfImpl( const char* in, const char* fmt, va_list args );

//  Minimal swscanf_s for 16-bit-WCHAR ASCII input/format.  Engine usage
//  is numeric tokens; we ignore the implicit buffer-size arguments the
//  _s form would normally require.
int swscanf_s( const wchar_t* wsz, const wchar_t* wszFmt, ... )
{
    char szInput[ 512 ];
    char szFmt[ 256 ];
    if ( !WideToNarrowAscii( wsz, szInput, sizeof( szInput ) ) ) return 0;
    if ( !WideToNarrowAscii( wszFmt, szFmt, sizeof( szFmt ) ) ) return 0;
    va_list ap;
    va_start( ap, wszFmt );
    int r = NarrowVSScanfImpl( szInput, szFmt, ap );
    va_end( ap );
    return r;
}

// Integer-to-ASCII helpers — engine code uses radix 10 only, but we accept
// 8/10/16 like the MSVC CRT. Returns 0 on success, EINVAL/ERANGE on error.
static int IntegerToAsciiCommon(long long value, bool isUnsigned, char* buffer, size_t cb, int radix)
{
    if (!buffer || cb == 0)
        return EINVAL;
    if (radix != 8 && radix != 10 && radix != 16)
    {
        buffer[0] = 0;
        return EINVAL;
    }
    int n = 0;
    if (radix == 10 && !isUnsigned)
    {
        n = snprintf(buffer, cb, "%lld", value);
    }
    else if (radix == 10)
    {
        n = snprintf(buffer, cb, "%llu", static_cast<unsigned long long>(value));
    }
    else if (radix == 16)
    {
        n = snprintf(buffer, cb, "%llx", static_cast<unsigned long long>(value));
    }
    else // radix == 8
    {
        n = snprintf(buffer, cb, "%llo", static_cast<unsigned long long>(value));
    }
    if (n < 0 || static_cast<size_t>(n) >= cb)
    {
        buffer[0] = 0;
        return ERANGE;
    }
    return 0;
}

// _wfopen_s — open a file using a wide path. The engine only opens stat-log
// files here, where paths and mode strings are ASCII. Converts both, then
// delegates to glibc fopen.
int _wfopen_s(FILE** ppf, const wchar_t* wszFile, const wchar_t* wszMode)
{
    if (!ppf)
        return EINVAL;
    *ppf = nullptr;
    char szPath[4096];
    char szMode[32];
    if (!WideToNarrowAscii(wszFile, szPath, sizeof(szPath)))
        return EINVAL;
    if (!WideToNarrowAscii(wszMode, szMode, sizeof(szMode)))
        return EINVAL;
    *ppf = fopen(szPath, szMode);
    return *ppf
           ? 0
           : errno;
}

int _ultoa_s(unsigned long value, char* buffer, size_t cb, int radix)
{
    return IntegerToAsciiCommon(static_cast<long long>(value), true, buffer, cb, radix);
}

int _ltoa_s(long value, char* buffer, size_t cb, int radix)
{
    return IntegerToAsciiCommon(static_cast<long long>(value), false, buffer, cb, radix);
}

int _itoa_s(int value, char* buffer, size_t cb, int radix)
{
    return IntegerToAsciiCommon(static_cast<long long>(value), false, buffer, cb, radix);
}

int _i64toa_s(long long value, char* buffer, size_t cb, int radix)
{
    return IntegerToAsciiCommon(value, false, buffer, cb, radix);
}

int _ui64toa_s(unsigned long long value, char* buffer, size_t cb, int radix)
{
    return IntegerToAsciiCommon(static_cast<long long>(value), true, buffer, cb, radix);
}

// _wsplitpath_s — Linux has no drive letters, so drive is always empty.
// Splits the wide path into dir + fname + ext using the last '/' and the
// last '.' (after that '/'). Each output buffer is optional.
static size_t WideLen(const wchar_t* s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

static int CopyWideRange(wchar_t* dst, size_t cch, const wchar_t* begin, const wchar_t* end)
{
    if (!dst || cch == 0)
        return EINVAL;
    const size_t need = (end >= begin)
                        ? static_cast<size_t>(end - begin)
                        : 0;
    if (need + 1 > cch)
    {
        dst[0] = 0;
        return ERANGE;
    }
    for (size_t i = 0; i < need; ++i)
        dst[i] = begin[i];
    dst[need] = 0;
    return 0;
}

int _wsplitpath_s(const wchar_t* wszPath,
    wchar_t* wszDrive, size_t cchDrive,
    wchar_t* wszDir, size_t cchDir,
    wchar_t* wszFname, size_t cchFname,
    wchar_t* wszExt, size_t cchExt)
{
    if (!wszPath)
        return EINVAL;
    if (wszDrive && cchDrive)
        wszDrive[0] = 0;
    const wchar_t* end = wszPath + WideLen(wszPath);
    const wchar_t* slash = end;
    while (slash > wszPath && slash[-1] != L'/' && slash[-1] != L'\\')
        --slash;
    const wchar_t* dot = end;
    for (const wchar_t* p = end; p > slash; --p)
    {
        if (p[-1] == L'.')
        {
            dot = p - 1;
            break;
        }
    }
    if (dot < slash)
        dot = end;
    int r = 0, t;
    if (wszDir && cchDir)
    {
        t = CopyWideRange(wszDir, cchDir, wszPath, slash);
        if (t)
            r = t;
    }
    if (wszFname && cchFname)
    {
        t = CopyWideRange(wszFname, cchFname, slash, dot);
        if (t)
            r = t;
    }
    if (wszExt && cchExt)
    {
        t = CopyWideRange(wszExt, cchExt, dot, end);
        if (t)
            r = t;
    }
    return r;
}

// _wmakepath_s — concatenate optional drive + dir + fname + ext into a
// single wide path. We ignore drive on Linux, but accept it for ABI.
static size_t WideAppend(wchar_t* dst, size_t cap, size_t n, const wchar_t* src)
{
    if (!src)
        return n;
    while (*src && n + 1 < cap) { dst[n++] = *src++; }
    dst[n] = 0;
    return n;
}

int _wmakepath_s(wchar_t* wszPath, size_t cchPath,
    const wchar_t* wszDrive,
    const wchar_t* wszDir,
    const wchar_t* wszFname,
    const wchar_t* wszExt)
{
    if (!wszPath || cchPath == 0)
        return EINVAL;
    size_t n = 0;
    wszPath[0] = 0;
    n = WideAppend(wszPath, cchPath, n, wszDrive);
    n = WideAppend(wszPath, cchPath, n, wszDir);
    // ensure trailing separator on dir if a fname follows
    if (wszFname && wszFname[0] && n > 0 && wszPath[n - 1] != L'/' && wszPath[n - 1] != L'\\')
    {
        if (n + 1 < cchPath)
        {
            wszPath[n++] = L'/';
            wszPath[n] = 0;
        }
        else
        {
            return ERANGE;
        }
    }
    n = WideAppend(wszPath, cchPath, n, wszFname);
    if (wszExt && wszExt[0] && wszExt[0] != L'.')
    {
        if (n + 1 < cchPath)
        {
            wszPath[n++] = L'.';
            wszPath[n] = 0;
        }
        else
            return ERANGE;
    }
    n = WideAppend(wszPath, cchPath, n, wszExt);
    return 0;
}

char* _strupr(char* s)
{
    if (!s)
        return s;
    for (char* p = s; *p; ++p)
    {
        *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    }
    return s;
}
} // extern "C"

//  printf / wprintf bridge.  Engine code (and Windows-style format
//  strings — %ws, %S, %I64x, %lu-as-32-bit, ...) routes through the
//  MSVC-compatible formatter in winapi_format.cxx.  Defining these here
//  preempts libc's symbols at link time (osposix.a is earlier in the
//  link order, same trick as wcslen / wcscmp etc. above).
extern "C" int NarrowVFPrintfImpl( FILE* fp, const char* fmt, va_list args );
extern "C" int WideVFPrintfImpl( FILE* fp, const wchar_t* fmt, va_list args );

extern "C"
{
int printf( const char* fmt, ... )
{
    va_list a;
    va_start( a, fmt );
    const int n = NarrowVFPrintfImpl( stdout, fmt, a );
    va_end( a );
    return n;
}

int fprintf( FILE* fp, const char* fmt, ... )
{
    va_list a;
    va_start( a, fmt );
    const int n = NarrowVFPrintfImpl( fp, fmt, a );
    va_end( a );
    return n;
}

int vprintf( const char* fmt, va_list args )
{
    return NarrowVFPrintfImpl( stdout, fmt, args );
}

int vfprintf( FILE* fp, const char* fmt, va_list args )
{
    return NarrowVFPrintfImpl( fp, fmt, args );
}

int wprintf( const wchar_t* fmt, ... )
{
    va_list a;
    va_start( a, fmt );
    const int n = WideVFPrintfImpl( stdout, fmt, a );
    va_end( a );
    return n;
}

int fwprintf( FILE* fp, const wchar_t* fmt, ... )
{
    va_list a;
    va_start( a, fmt );
    const int n = WideVFPrintfImpl( fp, fmt, a );
    va_end( a );
    return n;
}

int vwprintf( const wchar_t* fmt, va_list args )
{
    return WideVFPrintfImpl( stdout, fmt, args );
}

int vfwprintf( FILE* fp, const wchar_t* fmt, va_list args )
{
    return WideVFPrintfImpl( fp, fmt, args );
}

// fopen_s — narrow-path counterpart to _wfopen_s. Engine call sites use
// ASCII path and mode strings, so this is a thin wrapper over glibc fopen.
int fopen_s(FILE** ppf, const char* szFile, const char* szMode)
{
    if (!ppf)
    {
        return EINVAL;
    }
    *ppf = fopen(szFile, szMode);
    return *ppf
           ? 0
           : errno;
}

// wcstok_s — 16-bit-WCHAR strtok analogue. glibc's wcstok requires 32-bit
// wchar_t, which is unusable under -fshort-wchar. The engine only ever
// passes single-character delimiter strings (';'), so we keep the impl
// simple and check membership with a short loop. Stateful pointer is
// supplied by the caller; on the first call wsz is non-null, subsequent
// calls pass nullptr to continue scanning the remembered context.
wchar_t* wcstok_s(wchar_t* wsz, const wchar_t* wszDelim, wchar_t** ppwszCtx)
{
    if (!ppwszCtx || !wszDelim)
    {
        return nullptr;
    }

    wchar_t* p = wsz
                 ? wsz
                 : *ppwszCtx;
    if (!p)
    {
        return nullptr;
    }

    auto isDelim = [wszDelim](wchar_t c) -> bool
    {
        for (const wchar_t* d = wszDelim; *d; ++d)
        {
            if (c == *d)
            {
                return true;
            }
        }
        return false;
    };

    while (*p && isDelim(*p))
    {
        ++p;
    }
    if (!*p)
    {
        *ppwszCtx = nullptr;
        return nullptr;
    }

    wchar_t* tok = p;
    while (*p && !isDelim(*p))
    {
        ++p;
    }
    if (*p)
    {
        *p++ = 0;
        *ppwszCtx = p;
    }
    else
    {
        *ppwszCtx = nullptr;
    }
    return tok;
}
} // extern "C"
