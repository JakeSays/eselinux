// Linux shim for <intrin.h>. Maps the MSVC intrinsics the engine relies on
// to clang/GCC built-ins. Grow this file as new intrinsics surface.
#pragma once

#include <stdint.h>

// Clang's own <intrin.h> in its resource dir is gated on _MSC_VER (which
// our build doesn't set) and otherwise just `#include_next <intrin.h>`
// looking for MSVC's system header — which doesn't exist on Linux. So
// our shim has to provide what MSVC's <intrin.h> would.
//
// Most of the _BitScan*/_byteswap_* names are recognized by clang as
// builtins under -fms-extensions / -fms-compatibility — defining them
// as static inlines is rejected with "definition of builtin function".
// The fix is to *declare* them (prototype only). With a prototype in
// scope, clang inlines its builtin codegen at the call site. The few
// intrinsics clang doesn't recognize get static-inline bodies below.

#ifdef __cplusplus
extern "C" {
#endif

// Recognized as clang builtins under -fms-extensions; declarations only.
// Definitions would conflict with the builtin (-Werror=builtin-redefined).
//
// Spell the parameters in the exact base types clang's builtin uses
// (unsigned int / unsigned long long), NOT uint32_t/uint64_t — on
// Linux LP64, uint64_t aliases unsigned long, which clang's type
// system distinguishes from unsigned long long even though they are
// the same width. Likewise the Index pointer must be `unsigned int*`
// rather than `unsigned long*`.
unsigned char _BitScanReverse(unsigned int* Index, unsigned int Mask);
unsigned char _BitScanReverse64(unsigned int* Index, unsigned long long Mask);
unsigned char _BitScanForward(unsigned int* Index, unsigned int Mask);
unsigned char _BitScanForward64(unsigned int* Index, unsigned long long Mask);

unsigned short _byteswap_ushort(unsigned short x);
unsigned long  _byteswap_ulong(unsigned long x);
unsigned long long _byteswap_uint64(unsigned long long x);

#ifdef __cplusplus
} // extern "C"
#endif

// __rdtsc lives in <x86intrin.h> on clang/GCC; pull it in so MSVC code that
// just expects <intrin.h> to provide it Just Works.
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif
