// Linux shim for <intsafe.h>. The engine only uses a tiny subset
// (ULongAdd, ULongMult). Each function returns S_OK on success and
// INTSAFE_E_ARITHMETIC_OVERFLOW on overflow, matching the Windows
// contract.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef long HRESULT;
#endif

#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif

#define INTSAFE_E_ARITHMETIC_OVERFLOW   ((HRESULT)0x80070216L)

#ifdef __cplusplus
extern "C" {
#endif

// Windows ULONG is 32-bit; on Linux LP64 the engine's ULONG typedefs to
// uint32_t (unsigned int), not unsigned long. Take uint32_t* to match.
static inline HRESULT ULongAdd(uint32_t a, uint32_t b, uint32_t* out) {
    if (!out) return INTSAFE_E_ARITHMETIC_OVERFLOW;
    if (a > UINT32_MAX - b) { *out = UINT32_MAX; return INTSAFE_E_ARITHMETIC_OVERFLOW; }
    *out = a + b;
    return S_OK;
}

static inline HRESULT ULongMult(uint32_t a, uint32_t b, uint32_t* out) {
    if (!out) return INTSAFE_E_ARITHMETIC_OVERFLOW;
    uint64_t product = (uint64_t)a * (uint64_t)b;
    if (product > UINT32_MAX) { *out = UINT32_MAX; return INTSAFE_E_ARITHMETIC_OVERFLOW; }
    *out = (uint32_t)product;
    return S_OK;
}

#ifdef __cplusplus
} // extern "C"
#endif
