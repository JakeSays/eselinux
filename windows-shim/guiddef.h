// Linux shim for <guiddef.h>. Provides the GUID struct + IsEqualGUID, and
// declares an `IID` alias. Full COM interface plumbing is unused on the
// Linux port, so we keep this minimal.
#pragma once

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;

typedef GUID IID;
typedef GUID CLSID;
typedef GUID UUID;

typedef const GUID* LPCGUID;
typedef GUID* LPGUID;
typedef const IID* REFIID;
typedef const GUID* REFGUID;
typedef const CLSID* REFCLSID;

#ifdef __cplusplus
} // extern "C"

inline bool IsEqualGUID(const GUID& a, const GUID& b) {
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}

inline bool operator==(const GUID& a, const GUID& b) { return IsEqualGUID(a, b); }
inline bool operator!=(const GUID& a, const GUID& b) { return !IsEqualGUID(a, b); }
#endif

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern const GUID name
