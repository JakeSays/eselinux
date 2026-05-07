// Linux shim for <intrin.h>. Maps the MSVC intrinsics the engine relies on
// to clang/GCC built-ins. Grow this file as new intrinsics surface.
#pragma once

#include <stdint.h>

// Clang under -fms-extensions provides the _BitScan*, _byteswap_*, and
// related MSVC intrinsics as compiler builtins; redefining them here would
// clash. The shims below are for the GCC arm only (which we don't ship
// today, but keep working for future flexibility).
#if !defined(__clang__)

#ifdef __cplusplus
extern "C" {
#endif

static inline unsigned char _BitScanReverse(uint32_t* Index, uint32_t Mask) {
    if (Mask == 0) return 0;
    *Index = 31u - (uint32_t)__builtin_clz(Mask);
    return 1;
}

static inline unsigned char _BitScanReverse64(uint32_t* Index, uint64_t Mask) {
    if (Mask == 0) return 0;
    *Index = 63u - (uint32_t)__builtin_clzll(Mask);
    return 1;
}

static inline unsigned char _BitScanForward(uint32_t* Index, uint32_t Mask) {
    if (Mask == 0) return 0;
    *Index = (uint32_t)__builtin_ctz(Mask);
    return 1;
}

static inline unsigned char _BitScanForward64(uint32_t* Index, uint64_t Mask) {
    if (Mask == 0) return 0;
    *Index = (uint32_t)__builtin_ctzll(Mask);
    return 1;
}

static inline unsigned short _byteswap_ushort(unsigned short x) {
    return __builtin_bswap16(x);
}
static inline unsigned long _byteswap_ulong(unsigned long x) {
    return (unsigned long)__builtin_bswap32((uint32_t)x);
}
static inline uint64_t _byteswap_uint64(uint64_t x) {
    return __builtin_bswap64(x);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // !__clang__

// __rdtsc lives in <x86intrin.h> on clang/GCC; pull it in so MSVC code that
// just expects <intrin.h> to provide it Just Works.
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif
