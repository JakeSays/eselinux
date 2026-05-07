// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once

#ifndef _CC_HXX
#define _CC_HXX 1


// This is the C Compiler abstraction header file / library that isolates C 
// elements for common consumption across all of ESE dev and test code.

// Some interesting defines we might try ...
#ifdef _MSC_VER
#ifndef WINNT
//#define WINNT 1
#endif
#else
//#define UNIX  1
//#define _GCC  1
#endif

//
//      SAL is not defined everywhere
//

#ifndef _MSC_VER

// Legacy SAL annotations (__in_opt, __out_bcount, ...) are stubbed in the
// windows-shim copy of specstrings.h. The new-style _In_/_Out_ family
// follows below.
#include <specstrings.h>

// Modern (single-leading-underscore) SAL is stubbed out as no-ops on non-MSVC.
// Old-style (`__in_opt`, `__out_bcount`, ...) lives in windows-shim/specstrings.h.
#define _In_
#define _In_z_
#define _In_opt_
#define _In_opt_z_
#define _In_count_(x)
#define _In_count_x_(x)
#define _In_reads_(x)
#define _In_reads_opt_(x)
#define _In_reads_bytes_(x)
#define _In_reads_bytes_opt_(x)
#define _In_bytecount_(x)
#define _In_bytecount_c_(x)
#define _In_opt_bytecount_(x)
#define _In_opt_z_count_(x)
#define _In_z_count_c_(x)
#define _In_range_(x, y)

#define _Out_
#define _Out_opt_
#define _Out_writes_(x)
#define _Out_writes_to_(x, y)
#define _Out_writes_to_opt_(x, y)
#define _Out_writes_z_(x)
#define _Out_writes_opt_(x)
#define _Out_writes_opt_z_(x)
#define _Out_writes_bytes_(x)
#define _Out_writes_bytes_opt_(x)
#define _Out_writes_bytes_to_(x, y)
#define _Out_writes_bytes_to_opt_(x, y)
#define _Out_cap_(x)
#define _Out_cap_c_(x)
#define _Out_cap_post_count_(x, y)
#define _Out_opt_cap_(x)
#define _Out_opt_cap_post_count_(x, y)
#define _Out_opt_z_cap_post_count_(x, y)
#define _Out_z_cap_(x)
#define _Out_z_capcount_(x)
#define _Out_z_cap_post_count_(x, y)
#define _Out_z_bytecap_(x)
#define _Out_z_bytecap_post_bytecount_(x, y)
#define _Out_opt_z_bytecap_(x)
#define _Out_opt_z_bytecap_post_bytecount_(x, y)
#define _Out_bytecap_(x)
#define _Out_bytecap_c_(x)
#define _Out_bytecap_post_bytecount_(x, y)
#define _Out_bytecapcount_(x)
#define _Out_opt_bytecap_(x)

#define _Inout_
#define _Inout_z_
#define _Inout_opt_
#define _Inout_opt_z_
#define _Inout_bytecap_(x)
#define _Inout_bytecount_(x)
#define _Inout_updates_(x)
#define _Inout_updates_z_(x)
#define _Inout_updates_bytes_(x)
#define _Inout_updates_bytes_to_(x, y)
#define _Inout_updates_opt_(x)
#define _Inout_updates_opt_z_(x)
#define _Inout_updates_to_opt_(x, y)

#define _Outptr_
#define _Outptr_opt_
#define _Outptr_result_buffer_(x)
#define _Outptr_result_maybenull_
#define _Outptr_opt_result_maybenull_
#define _Outptr_opt_result_buffer_(x)
#define _Outptr_result_nullonfailure_
#define _Outptr_result_z_
#define _Outptr_opt_result_z_
#define _Outptr_result_bytebuffer_(x)
#define _Outref_

#define _Field_size_(x)
#define _Field_size_opt_(x)
#define _Field_size_bytes_(x)
#define _Field_size_bytes_opt_(x)
#define _Field_z_
#define _Field_range_(x, y)

#define _Pre_notnull_
#define _Pre_bytecap_(x)
#define _Pre_satisfies_(c)
#define _Post_invalid_
#define _Post_ptr_invalid_
#define _Post_satisfies_(c)
#define _Post_writable_byte_size_(x)
#define _Ret_maybenull_
#define _Ret_opt_z_
#define _Ret_range_(x, y)
#define _Ret_z_
#define _Deref_pre_z_
#define _Deref_in_z_
#define _Deref_inout_z_
#define _Deref_out_
#define _Deref_out_z_
#define _Deref_out_opt_z_
#define _Deref_opt_out_z_
#define _Deref_post_cap_(x)
#define _Deref_post_z_

#define _Null_terminated_
#define _Return_type_success_(x)
#define _When_(c, a)
#define _Use_decl_annotations_
#define _Reserved_
#define _Notnull_
#define _Maybenull_

// _At_(target, annotations) applies annotations to a specific target. _Curr_
// is the SAL placeholder for "the parameter being annotated"; both are
// analyzer-only on MSVC and elide cleanly on clang/GCC.
#define _At_(target, ...)
#define _Curr_

// PreFAST intrinsics. No-ops on non-MSVC (purely advisory to the analyzer).
#define __assume_bound(x)

#else // _MSC_VER

#include <sal.h>

// These conflict with definitions in headers such as <algorithm> on non-Windows platforms.
// _In_ and _Out_ should be used instead anyway, according to Microsoft's SAL documentation.
//
// Unfortunately, can't easily redefine __in and __out because these old-style annotations
// crept back into the Windows headers ntsecapi.h and dbgeng.h, which we include
//
// #undef __in
// #undef __out
//
// #define __in Use_In_instead_of__in
// #define __out Use_Out_instead_of__out

#endif // !_MSC_VER

#ifndef _MSC_VER
// glibc's <alloca.h> only exposes the __builtin_alloca macro under __GNUC__,
// which clang does not advertise under -fms-compatibility. Bring in alloca
// once at the bottom of the foundation header and force the builtin so every
// _malloca / alloca call site emits a stack alloc instead of an external
// function reference. cc.hxx is reached by every engine TU; windows-shim's
// own redefinition only covers the (smaller) set of TUs that include windows.h.
#include <alloca.h>
#undef alloca
#define alloca( size ) __builtin_alloca( ( size ) )
#endif // !_MSC_VER

//  Like SAL this produces a compile-time assert ...
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]



//
//      Types
//

#ifndef _MSC_VER
//  the required intXX_t types are std on VC - windows as well, but we can't define
//  them commonly b/c we get redefinition of basic types conflicts on WINNT.
#include <stdint.h>
#endif

//  odd void indirection
#pragma push_macro( "VOID" )
#undef VOID
typedef void VOID;
#pragma pop_macro( "VOID" )
typedef VOID * PVOID;

//  Boolean types
//

//  ESE's standard BOOL is 4 bytes, unlike bool which is 1 byte.  This is used
//  in a bunch of persisted structures and such, so changing it to bool is non-
//  trivial.  We will fix it at 4 bytes for now.  Besides if you really wanted
//  to save space, just use a bit-field.
#ifdef _MSC_VER
    typedef int                 BOOL;
#else
    typedef int32_t             BOOL;
#endif

//  Another complication, the signed BOOL and C++ bool are unsuitable for bit fields
//  of 1-bit size, due to the way C sign extends 1 to be 0xFFFFFFFF.  This type is
//  designed for usage in bit fields involving 4-byte types (INT, ULONG, etc) without 
//  these sign extension problems.
typedef unsigned int            FLAG32;

#define fFalse  BOOL( 0 )
#define fTrue   BOOL( !0 )

//  String types
//

typedef char                CHAR;
typedef CHAR                *LPSTR;


//  Basic integer types
//

#ifdef _MSC_VER
    typedef short               SHORT, *PSHORT;
    typedef unsigned short      USHORT, *PUSHORT;
    typedef int                 INT, *PINT;
    typedef unsigned int        UINT, *PUINT;
    typedef long                LONG, *PLONG;
    typedef unsigned long       ULONG, *PULONG;
    typedef long long           LONGLONG, *PLONGLONG;
    typedef unsigned long long  ULONGLONG, *PULONGLONG;
#else
    // On most other platforms, int and long are 64-bit on 64-bit platforms, but the ESE format
    // is dependent upon LONG being 32-bits.
    typedef int16_t             SHORT;
    typedef uint16_t            USHORT;
    typedef int32_t             INT;
    typedef uint32_t            UINT;
    typedef int32_t             LONG;
    typedef uint32_t            ULONG;
    typedef LONG*               PLONG;
    typedef ULONG*              PULONG;
    typedef SHORT*              PSHORT;
    typedef USHORT*             PUSHORT;
    typedef INT*                PINT;
    typedef UINT*               PUINT;
    // LONGLONG/ULONGLONG mirror MSVC's __int64-based definitions so that
    // overloads keyed on `__int64` and `LONGLONG` resolve to the same type.
    // (On x86_64 Linux, int64_t aliases `long`, not `long long`.)
    typedef long long           LONGLONG;
    typedef unsigned long long  ULONGLONG;
    typedef LONGLONG*           PLONGLONG;
    typedef ULONGLONG*          PULONGLONG;
#endif

//  Machine word types
//

typedef unsigned char       BYTE, *PBYTE;
typedef USHORT              WORD, *PWORD;
typedef ULONG               DWORD, *PDWORD;
typedef ULONGLONG           QWORD, *PQWORD;

#ifndef _MSC_VER
typedef char                CHAR, *PCHAR;
typedef unsigned char       UCHAR, *PUCHAR;
#endif

//  Pointer types
//

#if defined(_WIN64)
    #ifdef _MSC_VER
        typedef unsigned __int64    UNSIGNED_PTR;
        typedef __int64             SIGNED_PTR;
    #else // !_MSC_VER
        typedef unsigned long       UNSIGNED_PTR;
        typedef long                SIGNED_PTR;
    #endif // _MSC_VER
#else
    typedef unsigned long           UNSIGNED_PTR;
    typedef long                    SIGNED_PTR;
#endif


typedef LONGLONG            LONG64;
typedef unsigned int        DWORD32;
typedef unsigned int        ULONG32;
typedef ULONGLONG           ULONG64;

#ifndef _MSC_VER
typedef LONGLONG            INT64;
typedef ULONGLONG           UINT64;
typedef INT64*              PINT64;
typedef UINT64*             PUINT64;
#endif

//typedef long long         INT64;
//typedef unsigned long long    UINT64;
// On clang with -fms-extensions, __int64 is a builtin keyword (so that
// `unsigned __int64` parses correctly), so the typedef would clash.
// GCC without MS-extensions still needs the alias.
#if !defined(_MSC_VER) && !defined(__clang__)
    typedef long long           __int64;
#endif

// MSVC's <limits.h> exposes _I64_MAX / _UI64_MAX / _I32_MAX etc. as
// fixed-width integer limits. Map to the C99 stdint equivalents on Linux
// so engine call sites bind without #ifdef gates.
#ifndef _MSC_VER
    #include <stdint.h>
    #ifndef _I64_MAX
        #define _I64_MAX  INT64_MAX
    #endif
    #ifndef _I64_MIN
        #define _I64_MIN  INT64_MIN
    #endif
    #ifndef _UI64_MAX
        #define _UI64_MAX UINT64_MAX
    #endif
    #ifndef _I32_MAX
        #define _I32_MAX  INT32_MAX
    #endif
    #ifndef _I32_MIN
        #define _I32_MIN  INT32_MIN
    #endif
    #ifndef _UI32_MAX
        #define _UI32_MAX UINT32_MAX
    #endif
#endif

#if defined(_WIN64) || defined(__LP64__)
    #ifdef _MSC_VER

        typedef __int64 INT_PTR, *PINT_PTR;
        typedef unsigned __int64 UINT_PTR, *PUINT_PTR;

        typedef __int64 LONG_PTR, *PLONG_PTR;
        typedef unsigned __int64 ULONG_PTR, *PULONG_PTR;

    #else // !_MSC_VER

        typedef long long           INT_PTR, *PINT_PTR;
        typedef unsigned long long  UINT_PTR, *PUINT_PTR;

        typedef long long           LONG_PTR, *PLONG_PTR;
        typedef unsigned long long  ULONG_PTR, *PULONG_PTR;

    #endif // _MSC_VER
#else

    typedef __w64 int INT_PTR, *PINT_PTR;
    typedef __w64 unsigned int UINT_PTR, *PUINT_PTR;

    typedef __w64 long LONG_PTR, *PLONG_PTR;
    typedef __w64 unsigned long ULONG_PTR, *PULONG_PTR;

#endif

typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;
typedef ULONG_PTR SIZE_T, *PSIZE_T;

// Windows SDK exposes these via <intsafe.h>; outside MSVC we don't drag that
// header in, so define the size-of-pointer maxima here in terms of stdint.
#ifndef _MSC_VER
    #include <stdint.h>
    #ifndef UINT_PTR_MAX
        #define UINT_PTR_MAX  UINTPTR_MAX
    #endif
    #ifndef INT_PTR_MAX
        #define INT_PTR_MAX   INTPTR_MAX
    #endif
    #ifndef INT_PTR_MIN
        #define INT_PTR_MIN   INTPTR_MIN
    #endif
    #ifndef ULONG_PTR_MAX
        #define ULONG_PTR_MAX UINTPTR_MAX
    #endif
    #ifndef LONG_PTR_MAX
        #define LONG_PTR_MAX  INTPTR_MAX
    #endif
    #ifndef LONG_PTR_MIN
        #define LONG_PTR_MIN  INTPTR_MIN
    #endif
    #ifndef SIZE_T_MAX
        #define SIZE_T_MAX    SIZE_MAX
    #endif
    #ifndef DWORD_PTR_MAX
        #define DWORD_PTR_MAX UINTPTR_MAX
    #endif
#endif

//  Common project types
//

typedef _Return_type_success_( return >= 0 ) INT                ERR;



//
//      Limits
//
const USHORT    usMin   = 0x0000;
const USHORT    usMax   = 0xFFFF;

const LONG      lMin    = 0x80000000;
const LONG      lMax    = 0x7FFFFFFF;

const ULONG     ulMin   = 0x00000000;
const ULONG     ulMax   = 0xFFFFFFFF;

const LONG64    llMin   = 0x8000000000000000;
const LONG64    llMax   = 0x7FFFFFFFFFFFFFFF;

const ULONG64   ullMin  = 0x0000000000000000;
const ULONG64   ullMax  = 0xFFFFFFFFFFFFFFFF;

#if defined(_WIN64) || defined(__LP64__)
const UNSIGNED_PTR  upMin   = ullMin;
const UNSIGNED_PTR  upMax   = ullMax;
#else // !_WIN64
const UNSIGNED_PTR  upMin   = ulMin;
const UNSIGNED_PTR  upMax   = ulMax;
#endif // _WIN64

const QWORD     bMax    = 0xFF;
const QWORD     wMax    = 0xFFFF;
const QWORD     dwMax   = 0xFFFFFFFF;
const QWORD     qwMax   = 0xFFFFFFFFFFFFFFFF;

//
//      Declarative Defines
//

#ifndef _MSC_VER

    // Only the Microsoft VC++ in some build environment has alternate calling conventions as default at play and 
    // thus requires cdecl to be declared where we want the classic calling convention, so on we can just 
    // define this to nothing on UNIX (as everything is implicitly __cdecl there).
    #define __cdecl
    #define __stdcall

#endif // !_MSC_VER



//
//      Map commonly used CRT like pseudo functions
//

#ifndef _MSC_VER

    #define _stricmp strcasecmp

    // Microsoft CRT extensions implemented in os/posix/winapi_crt.cxx.
    // _wcsicmp can't reuse glibc's wcscasecmp because the engine builds with
    // -fshort-wchar (16-bit wchar_t) and wcscasecmp operates on 32-bit wchar_t.
    extern "C" int   _wcsicmp ( const wchar_t* s1, const wchar_t* s2 );
    extern "C" int   _wcsnicmp( const wchar_t* s1, const wchar_t* s2, size_t n );
    extern "C" int   _strnicmp( const char* s1, const char* s2, size_t n );
    extern "C" char* _strupr  ( char* s );
    // _wtol: wide-string to long. Width matches Windows MSVC's `long` (32 bits)
    // even on Linux LP64 — using LONG (which is int32_t in cc.hxx on non-MSVC).
    extern "C" LONG  _wtol    ( const wchar_t* s );
    extern "C" int   _wtoi    ( const wchar_t* s );

    // iswascii: wide-char ASCII predicate. Glibc's wctype.h only exposes this
    // when _GNU_SOURCE / XOPEN extensions are on, and even then it works on
    // 32-bit wchar_t. Inline equivalent works for our 16-bit wchar_t too.
    inline int iswascii( wchar_t c ) { return ( c & ~0x7F ) == 0; }

    // Microsoft's safe-CRT type for error returns (errno_t == int).
    typedef int errno_t;

    // rand_s: cryptographically secure random uint generator from MS CRT.
    // On Linux back it with getrandom(2); fall back to /dev/urandom is wrapped
    // by glibc, so this can't EAGAIN under normal conditions.
    extern "C" errno_t rand_s( unsigned int* pvalue );

    // swscanf_s: 16-bit-WCHAR ASCII variant. Format may contain MSVC %I64 / %I32
    // size specifiers; the impl translates them to glibc equivalents.
    extern "C" int swscanf_s( const wchar_t* wsz, const wchar_t* wszFmt, ... );

    // Wide-string to int64 (MSVC CRT). Engine uses radix 10 / 16 only.
    extern "C" unsigned long long _wcstoui64( const wchar_t* wsz, wchar_t** pwszEnd, int radix );
    extern "C" long long          _wcstoi64 ( const wchar_t* wsz, wchar_t** pwszEnd, int radix );

    // Narrow variants — devlibtest's iterquery suite uses these on parsed
    // ASCII test fixtures.
    extern "C" unsigned long long _strtoui64( const char* sz, char** pszEnd, int radix );
    extern "C" long long          _strtoi64 ( const char* sz, char** pszEnd, int radix );

    // _wfopen_s: ASCII-only wrapper over fopen for wide pathnames.
    struct _IO_FILE; // forward declare so we don't pull <stdio.h> here
    typedef struct _IO_FILE FILE;
    extern "C" errno_t _wfopen_s( FILE** ppf, const wchar_t* wszFile, const wchar_t* wszMode );

    // _wsplitpath_s / _wmakepath_s — wide-path split/join. Linux has no drive
    // letters so drive is always empty.
    extern "C" errno_t _wsplitpath_s( const wchar_t* wszPath,
                                      wchar_t* wszDrive, size_t cchDrive,
                                      wchar_t* wszDir,   size_t cchDir,
                                      wchar_t* wszFname, size_t cchFname,
                                      wchar_t* wszExt,   size_t cchExt );
    extern "C" errno_t _wmakepath_s ( wchar_t* wszPath, size_t cchPath,
                                      const wchar_t* wszDrive,
                                      const wchar_t* wszDir,
                                      const wchar_t* wszFname,
                                      const wchar_t* wszExt );

    // Integer-to-ASCII helpers from the MSVC safe-CRT. Linux glibc has no
    // direct equivalents (itoa is non-standard); shimmed via snprintf.
    extern "C" errno_t _ultoa_s ( unsigned long value, char* buffer, size_t cb, int radix );
    extern "C" errno_t _ltoa_s  ( long value,          char* buffer, size_t cb, int radix );
    extern "C" errno_t _itoa_s  ( int value,           char* buffer, size_t cb, int radix );
    extern "C" errno_t _i64toa_s( long long value,             char* buffer, size_t cb, int radix );
    extern "C" errno_t _ui64toa_s( unsigned long long value,   char* buffer, size_t cb, int radix );

    // _snprintf_s — MSVC's bounded snprintf. The 3rd param is a max-count
    // ("_TRUNCATE" or actual cap). We collapse to glibc's snprintf using the
    // smaller of the two limits, which matches the safe-CRT semantics for our
    // engine call sites (none of them rely on _TRUNCATE-specific behavior).
    #define _snprintf_s( buf, cb, cnt, fmt, ... ) snprintf( (buf), ( (cb) < (cnt) ? (cb) : (cnt) ), (fmt), ##__VA_ARGS__ )

    // sprintf_s — fixed-size sprintf. (buf, cb, fmt, ...) → snprintf,
    // ignoring the secure-CRT semantics for the trailing NUL.
    #define sprintf_s( buf, cb, fmt, ... ) snprintf( (buf), (cb), (fmt), ##__VA_ARGS__ )

#endif // !_MSC_VER



//
//      Basic "C operators"
//

#ifdef _MSC_VER
#define OffsetOf(s,m)   (SIZE_T)&(((s *)0)->m)
#else
#define OffsetOf(s,m)    __builtin_offsetof( s, m )
#endif

#define CONTAINING_RECORD(address, type, field) ((type *)( \
                                                  (PCHAR)(address) - \
                                                  (ULONG_PTR)(&((type *)0)->field)))

#ifdef _MSC_VER
//  No need - this set of operators (such as _countof()) is defined for MSVC tool set.
#else
#define _countof(rg)        ( sizeof(rg) / sizeof(rg[0]) )
#endif

#define _cbrg(rg)           ( _countof(rg) * sizeof(rg[0]) )


//
//      Compiler warning control
//

//  Only disable warnings that we think are valid and good coding practices here, if you are
//  dealing with a bad coding practice, the disable should be as narrowly scoped as possible.
#ifdef _MSC_VER

    #pragma warning ( disable : 4100 )  //  unreferenced formal parameter
    #pragma warning ( disable : 4201 )  //  we allow unnamed structs/unions
    #pragma warning ( disable : 4127 )  //  conditional expression is constant
    #pragma warning ( disable : 4355 )  //  we allow the use of this in ctor-inits
    #pragma warning ( disable : 4512 )  //  assignment operator could not be generated
    #pragma warning ( disable : 4706 )  //  assignment within conditional expression
    #pragma warning ( disable : 4786 )  //  we allow huge symbol names

    #ifdef DEBUG
    #else // DEBUG
        #pragma warning ( disable : 4189 )  //  local variable is initialized but not referenced
    #endif // !DEBUG

    #define Unused( var ) ( var )

#else // _MSC_VER

    // clang/GCC accept the same idiom; defining it on every arm keeps
    // call sites compiler-agnostic.
    #define Unused( var ) ( (void)( var ) )

#endif // _MSC_VER

#if !defined(BEGIN_PRAGMA_OPTIMIZE_DISABLE)
#define BEGIN_PRAGMA_OPTIMIZE_DISABLE(flags, bug, reason) \
    __pragma(optimize(flags, off))
#define BEGIN_PRAGMA_OPTIMIZE_ENABLE(flags, bug, reason) \
    __pragma(optimize(flags, on))
#define END_PRAGMA_OPTIMIZE() \
    __pragma(optimize("", on))
#endif


#endif // !_CC_HXX

