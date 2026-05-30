// Platform and architecture detection macros for the ESE Linux port.
//
// Prefer these over _WIN32/_WIN64/_M_AMD64/_MSC_VER throughout ESE source.
// The compiler-native macros are still defined by the compiler; these aliases
// just give a single, consistent vocabulary that works on both Windows and Linux.
//
// Included unconditionally by winnt.h before any other ESE header.
#pragma once

// ---------------------------------------------------------------------------
// OS
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#define ESE_OS_WINDOWS
#elif defined(__linux__)
#define ESE_OS_LINUX
#endif

// ---------------------------------------------------------------------------
// Architecture (CPU instruction set family)
// ---------------------------------------------------------------------------

#if defined(_M_AMD64) || defined(__x86_64__)
#define ESE_ARCH_AMD64
#elif defined(_M_IX86) || defined(__i386__)
#define ESE_ARCH_X86
#elif defined(_M_ARM64) || defined(__aarch64__)
#define ESE_ARCH_ARM64
#endif

// ---------------------------------------------------------------------------
// Pointer width (LP64 on Linux, LLP64 on Win64, ILP32 on Win32/Linux x32)
// ---------------------------------------------------------------------------

#if defined(_WIN64) || defined(__LP64__)
#define ESE_ARCH_64BIT
#else
#define ESE_ARCH_32BIT
#endif

// ---------------------------------------------------------------------------
// Compiler
// ---------------------------------------------------------------------------

#if defined(__clang__)
#define ESE_COMPILER_CLANG
#elif defined(_MSC_VER)
#define ESE_COMPILER_MSVC
#endif

// ---------------------------------------------------------------------------
// JET public base types — LP64 width override
// ---------------------------------------------------------------------------
// jethdr.w historically does `typedef long JET_INT32` / `unsigned long
// JET_UINT32` (correct on Windows, where long is 32-bit). On LP64 targets
// (Linux x86_64 / aarch64) long is 64-bit, which would silently widen every
// 32-bit JET integral type and break the public ABI. jethdr.w guards its
// base-types block with _JET_BASE_TYPES_DEFINED specifically so a platform may
// pre-define them; we use `int` / `unsigned int` (a true 32 bits on LP64) here.
// On Windows (LLP64 / ILP32) __LP64__ is undefined, so jethdr.w's own
// definitions stand unchanged.
#if defined(__LP64__)
#define _JET_BASE_TYPES_DEFINED
typedef char               JET_INT8;
typedef unsigned char      JET_UINT8;
typedef short              JET_INT16;
typedef unsigned short     JET_UINT16;
typedef int                JET_INT32;
typedef unsigned int       JET_UINT32;
typedef long long          JET_INT64;
typedef unsigned long long JET_UINT64;
typedef unsigned char      JET_BYTE;
typedef void               JET_VOID;
typedef void *             JET_PVOID;
typedef const void *       JET_PCVOID;
typedef char               JET_CHAR;
#if !defined(_NATIVE_WCHAR_T_DEFINED)
typedef unsigned short     JET_WCHAR;
#else
typedef wchar_t            JET_WCHAR;
#endif
#endif // __LP64__
