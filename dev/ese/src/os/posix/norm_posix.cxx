// POSIX equivalent of os/norm.cxx.
//
// Backs the engine's secondary-index sort/compare path with ICU rather
// than Win32 NLS. The on-disk sort-key bytes therefore differ from a
// Windows ESE build's bytes — that's intentional (the Linux port is
// not Windows-format-compatible; see the project memory).
//
// Mapping summary:
//
//   LCMAP_SORTKEY        -> ucol_getSortKey
//   LCMAP_UPPERCASE      -> u_strToUpper, written big-endian to match
//                           the upstream rgbSeg layout
//   NORM_IGNORECASE      -> UCOL_STRENGTH = SECONDARY (drops tertiary
//                           case differences)
//   NORM_IGNOREKANATYPE  -> UCOL_HIRAGANA_QUATERNARY_MODE = OFF (ICU's
//                           default already merges hiragana/katakana at
//                           tertiary level)
//   NORM_IGNOREWIDTH     -> no direct ICU equivalent; ignored. ICU's
//                           default tertiary collation merges most
//                           halfwidth/fullwidth pairs at the primary
//                           level which is good enough for v1.
//
//   ErrNORMGetSortVersion encodes ucol_getVersion (NLS slot) and
//   u_getUnicodeVersion (defined slot) into the QWORD layout the
//   engine persists alongside each locale-tagged index. These are ICU
//   versions, not NLS versions; an index built with ICU 74 won't
//   validate under ICU 76 if the collation tables change. Engines
//   that need durable index validity across ICU upgrades should pin
//   the ICU data version explicitly — out of scope for v1.
//
// Thread safety: UCollator instances cached in CollatorCache() have
// their attributes set once at construction and never mutated after.
// ICU documents ucol_getSortKey as thread-safe under that constraint.

#include "osstd.hxx"

// ICU and libc++ system headers must be reached without the MEM_CHECK
// `#define new ...` from memory.hxx in scope; that macro mangles
// `operator new(size_t)` declarations inside ICU's localpointer.h and
// libc++'s <concepts>/<algorithm>. The PCH brings memory.hxx in before
// this TU runs, so push/undef around the system includes here.
#pragma push_macro("new")
#undef new
#include <unicode/ucol.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>
#include <unicode/uversion.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#pragma pop_macro("new")

const WORD sortidNone = SORT_DEFAULT;
const LANGID langidNone = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
const LANGID langidInvariant = MAKELANGID(LANG_INVARIANT, SUBLANG_NEUTRAL);

const LCID lcidNone = MAKELCID(langidNone, sortidNone);
const LCID lcidInvariant = MAKELCID(langidInvariant, sortidNone);

const PWSTR wszLocaleNameDefault = (PWSTR) L"en-US";
const PWSTR wszLocaleNameNone = (PWSTR) L"";

const DWORD dwLCMapFlagsDefault = (LCMAP_SORTKEY | NORM_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH);

const DWORD dwLCMapFlagsDefaultOBSOLETE = (LCMAP_SORTKEY | NORM_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH);

namespace
{
//  Convert a 16-bit-WCHAR locale name ("en-US", "ja-JP_phonebook")
//  to ASCII for ICU. ICU locale IDs are 7-bit ASCII; any non-ASCII
//  byte indicates a malformed input and we return an empty string
//  to make the caller fail.
std::string LocaleToAscii(PCWSTR wszLocale)
{
    std::string out;
    if (!wszLocale)
    {
        return out;
    }
    for (; *wszLocale; ++wszLocale)
    {
        const WCHAR ch = *wszLocale;
        if (ch >= 0x80)
        {
            return std::string();
        }
        //  ICU prefers '_' as the script/variant separator. Locale
        //  names from the engine arrive as Win32 BCP-47 ("ja-JP")
        //  which uloc_canonicalize handles. Pass through verbatim.
        out.push_back((char) ch);
    }
    return out;
}

//  std::u16string is portable and always 16-bit, independent of
//  -fshort-wchar. Use it as the cache key so we don't depend on
//  libc++'s std::wstring (built without -fshort-wchar in the
//  upstream tarball).
std::u16string LocaleToU16(PCWSTR wszLocale)
{
    std::u16string out;
    if (!wszLocale)
        return out;
    for (; *wszLocale; ++wszLocale)
    {
        out.push_back((char16_t) *wszLocale);
    }
    return out;
}

struct CollatorKey
{
    std::u16string locale;
    DWORD flags;

    bool operator==(const CollatorKey& other) const
    {
        return flags == other.flags && locale == other.locale;
    }
};

struct CollatorKeyHash
{
    size_t operator()(const CollatorKey& k) const noexcept
    {
        return std::hash<std::u16string>{}(k.locale) ^ ((size_t) k.flags * 0x9E3779B97F4A7C15ull);
    }
};

std::mutex& CollatorMutex()
{
    static std::mutex m;
    return m;
}

std::unordered_map<CollatorKey, UCollator*, CollatorKeyHash>& CollatorCache()
{
    static std::unordered_map<CollatorKey, UCollator*, CollatorKeyHash> c;
    return c;
}

//  Map LCMAP/NORM_* flags onto the collator's strength + attributes.
//  Called exactly once per cached collator while still under the
//  cache mutex so the resulting collator is effectively immutable.
void ApplyFlagsToCollator(UCollator* coll, DWORD flags)
{
    UErrorCode status = U_ZERO_ERROR;

    //  NORM_IGNORECASE -> drop tertiary level (case is encoded at
    //  tertiary in UCA). Without it we use the engine's "default"
    //  which is also tertiary-sensitive.
    if (flags & NORM_IGNORECASE)
    {
        ucol_setStrength(coll, UCOL_SECONDARY);
    }
    else
    {
        ucol_setStrength(coll, UCOL_TERTIARY);
    }

    //  NORM_IGNOREKANATYPE: ICU's default already merges hira/kata
    //  at tertiary; turning the quaternary mode off is the closest
    //  analogue to the Win32 flag. Errors are non-fatal — unknown
    //  attributes on older ICU just leave the default.
    ucol_setAttribute(coll, UCOL_HIRAGANA_QUATERNARY_MODE,
        (flags & NORM_IGNOREKANATYPE)
        ? UCOL_OFF
        : UCOL_ON,
        &status);
    status = U_ZERO_ERROR;
}

//  Look up or create a UCollator for (locale, flags). Returns nullptr
//  on failure; caller should map that to JET_errInvalidLanguageId.
UCollator* GetCollator(PCWSTR wszLocale, DWORD flags)
{
    const CollatorKey key{LocaleToU16(wszLocale), flags};

    std::lock_guard<std::mutex> guard(CollatorMutex());
    auto& cache = CollatorCache();
    auto it = cache.find(key);
    if (it != cache.end())
    {
        return it->second;
    }

    const std::string locale = LocaleToAscii(wszLocale);
    if (locale.empty() && wszLocale && *wszLocale)
    {
        return nullptr;
    }

    UErrorCode status = U_ZERO_ERROR;
    UCollator* coll = ucol_open(locale.c_str(), &status);
    if (U_FAILURE(status) || !coll)
    {
        if (coll)
        {
            ucol_close(coll);
        }
        return nullptr;
    }

    ApplyFlagsToCollator(coll, flags);
    cache.emplace(key, coll);
    return coll;
}

void DestroyCollatorCache()
{
    std::lock_guard<std::mutex> guard(CollatorMutex());
    for (auto& kv : CollatorCache())
    {
        ucol_close(kv.second);
    }
    CollatorCache().clear();
}

std::atomic<bool> g_fNormInited{false};
}


////////////////////////////////////////////////
//  Pure helpers — verbatim from upstream

const LANGID LangidFromLcid(const LCID lcid)
{
    return LANGIDFROMLCID(lcid);
}

BOOL FNORMLCMapFlagsHasUpperCase(DWORD dwMapFlags)
{
    return !!(dwMapFlags & LCMAP_UPPERCASE);
}

#define __ascii_towlower(c)      ( (((c) >= L'A') && ((c) <= L'Z')) ? ((c) - L'A' + L'a') : (c) )

static INT __ese_wcsicmp(const wchar_t* wszLocale1, const wchar_t* wszLocale2)
{
    wchar_t f, l;
    do
    {
        f = __ascii_towlower(*wszLocale1);
        l = __ascii_towlower(*wszLocale2);
        wszLocale1++;
        wszLocale2++;
    } while ((f) && (f == l));
    return (INT) (f - l);
}

INT NORMCompareLocaleName(PCWSTR const wszLocale1, PCWSTR const wszLocale2)
{
    return __ese_wcsicmp(wszLocale1, wszLocale2);
}

BOOL FNORMEqualsLocaleName(PCWSTR const wszLocale1, PCWSTR const wszLocale2)
{
    return (0 == NORMCompareLocaleName(wszLocale1, wszLocale2));
}

BOOL FNORMNLSVersionEquals(QWORD qwVersionCreated, QWORD qwVersionCurrent)
{
    return qwVersionCreated == qwVersionCurrent;
}


////////////////////////////////////////////////
//  Stub LCMapStringEx — kept around as a link-time fallback for any
//  engine code path we missed. Real collation goes through ICU below.

int LCMapStringEx(
    LPCWSTR /* lpLocaleName */,
    DWORD /* dwMapFlags */,
    LPCWSTR /* lpSrcStr */,
    int /* cchSrc */,
    LPWSTR /* lpDestStr */,
    int /* cchDest */,
    LPNLSVERSIONINFO /* lpVersionInformation */,
    LPVOID /* lpReserved */,
    DWORD_PTR /* lParam */)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}


////////////////////////////////////////////////
//  Locale validation, version, and conversion

BOOL FNORMGetNLSExIsSupported()
{
    //  The engine guards "modern" code paths (NLSVERSIONINFOEX, sort
    //  GUIDs, etc.) on this. With ICU we always have the modern surface.
    return fTrue;
}

ERR ErrNORMCheckLocaleName(_In_ INST* const /* pinst */, __in_z PCWSTR const wszLocaleName)
{
    if (!wszLocaleName)
    {
        return ErrERRCheck(JET_errInvalidLanguageId);
    }

    //  An ICU collator open is the cheapest authoritative validity test
    //  for a locale ID; cache it so repeat calls are free.
    if (!GetCollator(wszLocaleName, dwLCMapFlagsDefault))
    {
        return ErrERRCheck(JET_errInvalidLanguageId);
    }
    return JET_errSuccess;
}

ERR ErrNORMCheckLocaleVersion(_In_ const NORM_LOCALE_VER* pnlv)
{
    if (!pnlv)
    {
        return ErrERRCheck(JET_errInvalidParameter);
    }
    return ErrNORMCheckLocaleName(nullptr, pnlv->m_wszLocaleName);
}

ERR ErrNORMCheckLCMapFlags(_In_ INST* const /* pinst */,
    _In_ const DWORD /* dwLCMapFlags */,
    _In_ const BOOL /* fUppercaseTextNormalization */)
{
    return JET_errSuccess;
}

ERR ErrNORMCheckLCMapFlags(_In_ INST* const /* pinst */,
    _Inout_ DWORD* const pdwLCMapFlags,
    _In_ const BOOL /* fUppercaseTextNormalization */)
{
    if (pdwLCMapFlags && *pdwLCMapFlags == 0)
    {
        *pdwLCMapFlags = dwLCMapFlagsDefault;
    }
    return JET_errSuccess;
}

ERR ErrNORMGetSortVersion(__in_z PCWSTR wszLocaleName,
    _Out_ QWORD* const pqwVersion,
    __out_opt SORTID* const psortID,
    _In_ const BOOL /* fErrorOnInvalidId */)
{
    if (pqwVersion) { *pqwVersion = 0; }
    if (psortID) { memset(psortID, 0, sizeof(*psortID)); }

    UCollator* coll = GetCollator(wszLocaleName, dwLCMapFlagsDefault);
    if (!coll)
    {
        return ErrERRCheck(JET_errInvalidLanguageId);
    }

    //  Pack ucol_getVersion (collator UCA + tailoring) into the NLS
    //  slot and u_getUnicodeVersion into the defined slot. Both are
    //  4-byte UVersionInfo arrays.
    UVersionInfo collVer = {0, 0, 0, 0};
    UVersionInfo uniVer = {0, 0, 0, 0};
    ucol_getVersion(coll, collVer);
    u_getUnicodeVersion(uniVer);

    auto pack = [](const UVersionInfo v) -> DWORD
    {
        return ((DWORD) v[0] << 24) | ((DWORD) v[1] << 16)
            | ((DWORD) v[2] << 8) | ((DWORD) v[3]);
    };

    if (pqwVersion)
    {
        *pqwVersion = QwSortVersionFromNLSDefined(pack(collVer), pack(uniVer));
    }
    return JET_errSuccess;
}

ERR ErrNORMLcidToLocale(
    _In_ const LCID /* lcid */,
    __out_ecount(cchLocale) PWSTR wszLocale,
    _In_ ULONG cchLocale)
{
    //  LCID is a Win32 concept — the engine only relies on this on
    //  the legacy path that's already being phased out. Hand back the
    //  default locale name; downstream validation goes through ICU.
    if (cchLocale == 0 || !wszLocale)
    {
        return ErrERRCheck(JET_errBufferTooSmall);
    }
    const PWSTR src = wszLocaleNameDefault;
    ULONG i = 0;
    for (; src[i] && i + 1 < cchLocale; ++i)
        wszLocale[i] = src[i];
    wszLocale[i] = L'\0';
    return JET_errSuccess;
}

ERR ErrNORMLocaleToLcid(
    __in_z PCWSTR /* wszLocale */,
    _Out_ LCID* plcid)
{
    if (plcid) { *plcid = lcidInvariant; }
    return JET_errSuccess;
}


////////////////////////////////////////////////
//  ErrNORMMapString — the hot path. Two output formats:
//   * LCMAP_SORTKEY: opaque sort-key bytes from ucol_getSortKey
//   * LCMAP_UPPERCASE: uppercased UTF-16, written BIG-ENDIAN into rgbSeg
//     plus a trailing WCHAR null. (Big-endian matches the upstream
//     UnalignedBigEndian<WCHAR> writer in os/norm.cxx so the engine's
//     index-key compare path doesn't change.)

namespace
{
ERR ErrNORMMapStringSortKey(const NORM_LOCALE_VER* pnlv,
    const BYTE* pbColumn,
    INT cbColumn,
    BYTE* rgbSeg,
    INT cbMax,
    INT* pcbSeg)
{
    UCollator* coll = GetCollator(pnlv->m_wszLocaleName,
        pnlv->m_dwNormalizationFlags);
    if (!coll)
    {
        return ErrERRCheck(JET_errInvalidLanguageId);
    }

    const UChar* src = reinterpret_cast<const UChar*>(pbColumn);
    const int32_t cchSrc = (int32_t) (cbColumn / sizeof(WCHAR));

    //  ucol_getSortKey:
    //   * returns 0 on internal failure (rare; treat as translation fail)
    //   * returns required size if buffer too small (writes truncated
    //     prefix into rgbSeg). Required size includes the 0x00 terminator.
    const int32_t cb = ucol_getSortKey(coll, src, cchSrc, rgbSeg, cbMax);
    if (cb == 0)
    {
        return ErrERRCheck(JET_errUnicodeTranslationFail);
    }
    if (cb > cbMax)
    {
        //  Index key truncated. Engine treats wrnFLDKeyTooBig as a
        //  warning — fill *pcbSeg with the bytes we did write.
        *pcbSeg = cbMax;
        return ErrERRCheck(wrnFLDKeyTooBig);
    }

    *pcbSeg = cb;
    return JET_errSuccess;
}

ERR ErrNORMMapStringUppercase(const NORM_LOCALE_VER* pnlv,
    const BYTE* pbColumn,
    INT cbColumn,
    BYTE* rgbSeg,
    INT cbMax,
    INT* pcbSeg)
{
    const std::string locale = LocaleToAscii(pnlv->m_wszLocaleName);
    if (locale.empty() && pnlv->m_wszLocaleName[0])
    {
        return ErrERRCheck(JET_errInvalidLanguageId);
    }

    const UChar* src = reinterpret_cast<const UChar*>(pbColumn);
    const int32_t cchSrc = (int32_t) (cbColumn / sizeof(WCHAR));

    //  Probe the required size first. ICU returns the desired length
    //  even when the dest is null/zero-length (it sets U_BUFFER_OVERFLOW_ERROR).
    UErrorCode status = U_ZERO_ERROR;
    const int32_t cchUpper = u_strToUpper(nullptr, 0, src, cchSrc,
        locale.c_str(), &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
    {
        return ErrERRCheck(JET_errUnicodeTranslationFail);
    }

    const size_t cbUpper = (size_t) cchUpper * sizeof(WCHAR);
    const size_t cbUpperWithNul = cbUpper + sizeof(WCHAR);

    //  Match upstream: cbMax must hold uppercased bytes plus a
    //  trailing null WCHAR.
    if ((INT) cbUpperWithNul > cbMax)
    {
        *pcbSeg = cbMax;
        return ErrERRCheck(wrnFLDKeyTooBig);
    }

    //  Stage into a temp buffer so we can byte-swap into rgbSeg.
    BYTE* tmp = (BYTE*) PvOSMemoryHeapAlloc(cbUpperWithNul);
    if (!tmp)
    {
        return ErrERRCheck(JET_errOutOfMemory);
    }

    status = U_ZERO_ERROR;
    u_strToUpper(reinterpret_cast<UChar*>(tmp), cchUpper,
        src, cchSrc, locale.c_str(), &status);
    if (U_FAILURE(status))
    {
        OSMemoryHeapFree(tmp);
        return ErrERRCheck(JET_errUnicodeTranslationFail);
    }

    //  Big-endian write — matches UnalignedBigEndian<WCHAR> in upstream.
    for (int32_t i = 0; i < cchUpper; ++i)
    {
        const WCHAR ch = reinterpret_cast<const WCHAR*>(tmp)[i];
        rgbSeg[i * 2] = (BYTE) ((ch >> 8) & 0xFF);
        rgbSeg[i * 2 + 1] = (BYTE) (ch & 0xFF);
    }
    //  Trailing null WCHAR (byte order doesn't matter for zero).
    rgbSeg[cchUpper * 2] = 0;
    rgbSeg[cchUpper * 2 + 1] = 0;

    OSMemoryHeapFree(tmp);
    *pcbSeg = (INT) cbUpperWithNul;
    return JET_errSuccess;
}
}

ERR ErrNORMMapString(
    _In_ const NORM_LOCALE_VER* pnlv,
    _In_reads_(cbColumn) BYTE* pbColumn,
    _In_ INT cbColumn,
    _Out_writes_to_(cbMax, *pcbSeg) BYTE* const rgbSeg,
    _In_ const INT cbMax,
    _Out_ INT* const pcbSeg)
{
    if (!pnlv || !pbColumn || !rgbSeg || !pcbSeg)
    {
        if (pcbSeg) { *pcbSeg = 0; }
        return ErrERRCheck(JET_errInvalidParameter);
    }

    *pcbSeg = 0;

    if (cbColumn <= 0 || (cbColumn % sizeof(WCHAR)) != 0)
    {
        return ErrERRCheck(JET_errInvalidParameter);
    }

    if (pnlv->m_dwNormalizationFlags == LCMAP_UPPERCASE)
    {
        return ErrNORMMapStringUppercase(pnlv, pbColumn, cbColumn,
            rgbSeg, cbMax, pcbSeg);
    }

    if (pnlv->m_dwNormalizationFlags & LCMAP_SORTKEY)
    {
        return ErrNORMMapStringSortKey(pnlv, pbColumn, cbColumn,
            rgbSeg, cbMax, pcbSeg);
    }

    return ErrERRCheck(JET_errInvalidParameter);
}


#ifdef DEBUG
//  AssertNORMConstants() — DEBUG-only sanity checks on the Win32 NLS
//  flag values (LCMAP_SORTKEY, NORM_IGNORECASE, ...) the engine persists
//  into index headers. norm.hxx declares the function in DEBUG and
//  expands to a no-op #define in retail; matching the gate keeps the
//  PCH consistent. The Linux port routes collation through ICU rather
//  than NLS, so the constants are irrelevant — the body is empty.
VOID AssertNORMConstants()
{
}
#endif


////////////////////////////////////////////////
//  Lifecycle

void OSNormPostterm()
{
    DestroyCollatorCache();
    g_fNormInited.store(false, std::memory_order_release);
}

BOOL FOSNormPreinit()
{
    return fTrue;
}

void OSNormTerm()
{
    DestroyCollatorCache();
    g_fNormInited.store(false, std::memory_order_release);
}

ERR ErrOSNormInit()
{
    if (!g_fNormInited.exchange(true, std::memory_order_acq_rel))
    {
        //  Force a default collator open so any ICU data-loading
        //  failure surfaces here rather than mid-transaction.
        UErrorCode status = U_ZERO_ERROR;
        UCollator* coll = ucol_open("en-US", &status);
        if (U_FAILURE(status) || !coll)
        {
            if (coll)
                ucol_close(coll);
            g_fNormInited.store(false, std::memory_order_release);
            return ErrERRCheck(JET_errUnicodeLanguageValidationFailure);
        }
        ucol_close(coll);
    }
    return JET_errSuccess;
}
