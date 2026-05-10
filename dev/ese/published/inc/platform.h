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
