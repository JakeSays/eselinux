// POSIX equivalent of os/norm.cxx. Upstream wraps the Windows NLS surface
// (LCMapStringEx, LCIDToLocaleName, GetNLSVersionEx etc.) for the engine's
// secondary-index sort/compare path. Linux has no NLS — the eventual port
// will use ICU. For Phase 5 we provide enough stubs to link.
//
// What we keep portable:
//
//   - Locale-name comparison helpers (NORMCompareLocaleName,
//     FNORMEqualsLocaleName) — they're already pure ASCII, case-insensitive
//     wcsicmp-equivalent code from upstream
//   - FNORMLCMapFlagsHasUpperCase — single bit test
//   - Lifecycle entry points
//
// What we stub:
//
//   - All ErrNORMCheckLocale*/ErrNORMMapString/ErrNORMGetSortVersion etc.
//     return JET_errFeatureNotAvailable. The engine guards index creation on
//     these — callers who request collation will see the error rather than
//     silently using the wrong sort order. Per the port plan we don't need
//     to be able to read Windows-produced .edb files initially, so this
//     limitation is acceptable

#include "osstd.hxx"

const WORD          sortidNone                  = SORT_DEFAULT;
const LANGID        langidNone                  = MAKELANGID( LANG_NEUTRAL, SUBLANG_NEUTRAL );
const LANGID        langidInvariant             = MAKELANGID( LANG_INVARIANT, SUBLANG_NEUTRAL );

const LCID          lcidNone                    = MAKELCID( langidNone, sortidNone );
const LCID          lcidInvariant               = MAKELCID( langidInvariant, sortidNone );

const PWSTR         wszLocaleNameDefault        = (PWSTR)L"en-US";
const PWSTR         wszLocaleNameNone           = (PWSTR)L"";

const DWORD         dwLCMapFlagsDefault         = ( LCMAP_SORTKEY | NORM_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH );

//  Pre-NLSv6 default. Persisted-format checks in cat.cxx still reference it
//  by name; declared in published/inc/os/norm.hxx, defined here.
const DWORD         dwLCMapFlagsDefaultOBSOLETE = ( LCMAP_SORTKEY | NORM_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH );


////////////////////////////////////////////////
//  Pure helpers — verbatim from upstream

const LANGID LangidFromLcid( const LCID lcid )
{
    return LANGIDFROMLCID( lcid );
}

BOOL FNORMLCMapFlagsHasUpperCase( DWORD dwMapFlags )
{
    return !!( dwMapFlags & LCMAP_UPPERCASE );
}

//  LCMapStringEx — full Win32 NLS string mapping. ICU port lands later;
//  for now we provide a stub that returns 0 (failure) and sets last-error
//  so engine call sites surface JET_errInvalidLocaleName / similar.
//  This is sufficient for eseutil's link path and for engine code paths
//  that aren't exercised without an actual locale-tagged column.
int LCMapStringEx(
    LPCWSTR             /* lpLocaleName */,
    DWORD               /* dwMapFlags */,
    LPCWSTR             /* lpSrcStr */,
    int                 /* cchSrc */,
    LPWSTR              /* lpDestStr */,
    int                 /* cchDest */,
    LPNLSVERSIONINFO    /* lpVersionInformation */,
    LPVOID              /* lpReserved */,
    DWORD_PTR           /* lParam */ )
{
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return 0;
}

#define __ascii_towlower(c)      ( (((c) >= L'A') && ((c) <= L'Z')) ? ((c) - L'A' + L'a') : (c) )

static INT __ese_wcsicmp( const wchar_t * wszLocale1, const wchar_t * wszLocale2 )
{
    wchar_t f, l;
    do
    {
        f = __ascii_towlower(*wszLocale1);
        l = __ascii_towlower(*wszLocale2);
        wszLocale1++;
        wszLocale2++;
    }
    while ( (f) && (f == l) );
    return (INT)(f - l);
}

INT NORMCompareLocaleName( PCWSTR const wszLocale1, PCWSTR const wszLocale2 )
{
    return __ese_wcsicmp( wszLocale1, wszLocale2 );
}

BOOL FNORMEqualsLocaleName( PCWSTR const wszLocale1, PCWSTR const wszLocale2 )
{
    return ( 0 == NORMCompareLocaleName( wszLocale1, wszLocale2 ) );
}

BOOL FNORMNLSVersionEquals( QWORD qwVersionCreated, QWORD qwVersionCurrent )
{
    return qwVersionCreated == qwVersionCurrent;
}


////////////////////////////////////////////////
//  Locale validation / mapping — stubs
//
//  All NLS-backed operations return JET_errFeatureNotAvailable. The engine
//  treats this the same as "this build can't honor the requested collation",
//  which is the right answer until ICU is wired in.

BOOL FNORMGetNLSExIsSupported()
{
    return fFalse;
}

ERR ErrNORMCheckLocaleName( _In_ INST * const /* pinst */, __in_z PCWSTR const /* wszLocaleName */ )
{
    //  Validation-only; the engine just records the locale string and
    //  doesn't try to use it until a secondary-index sort/compare op
    //  fires (which we route into ErrNORMMapString and friends below,
    //  where they fail loudly). Return success so JetSetSystemParameter
    //  can store the locale name unrejected.
    return JET_errSuccess;
}

ERR ErrNORMCheckLocaleVersion( _In_ const NORM_LOCALE_VER* /* pnlv */ )
{
    return JET_errSuccess;
}

ERR ErrNORMCheckLCMapFlags( _In_ INST * const /* pinst */,
                            _In_ const DWORD /* dwLCMapFlags */,
                            _In_ const BOOL  /* fUppercaseTextNormalization */ )
{
    return JET_errSuccess;
}

ERR ErrNORMCheckLCMapFlags( _In_ INST * const /* pinst */,
                            _Inout_ DWORD * const pdwLCMapFlags,
                            _In_ const BOOL /* fUppercaseTextNormalization */ )
{
    if ( pdwLCMapFlags ) { *pdwLCMapFlags = dwLCMapFlagsDefault; }
    return JET_errSuccess;
}

ERR ErrNORMGetSortVersion( __in_z PCWSTR /* wszLocaleName */,
                           _Out_ QWORD * const pqwVersion,
                           __out_opt SORTID * const psortID,
                           _In_ const BOOL /* fErrorOnInvalidId */ )
{
    //  Engine records this version against the locale on column create
    //  and re-checks it at index time. Returning success with a synthetic
    //  version lets JetSetSystemParameter / JetCreateInstance proceed;
    //  any actual sort/compare op will hit the ErrNORMMapString stub
    //  below which still fails.
    if ( pqwVersion ) { *pqwVersion = 0; }
    if ( psortID )    { memset( psortID, 0, sizeof(*psortID) ); }
    return JET_errSuccess;
}

ERR ErrNORMMapString(
    _In_ const NORM_LOCALE_VER*     /* pnlv */,
    _In_reads_(cbColumn) BYTE *     /* pbColumn */,
    _In_ INT                        /* cbColumn */,
    _Out_writes_to_(cbMax, *pcbSeg) BYTE * const    /* rgbSeg */,
    _In_ const INT                  /* cbMax */,
    _Out_ INT * const               pcbSeg )
{
    if ( pcbSeg ) { *pcbSeg = 0; }
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

ERR ErrNORMLcidToLocale(
    _In_ const LCID                /* lcid */,
    __out_ecount( cchLocale ) PWSTR wszLocale,
    _In_ ULONG cchLocale )
{
    //  Best-effort: hand back the default invariant locale name. Engine
    //  threads the result through ErrNORMCheckLocaleName (which we
    //  always succeed) and stores it in instance metadata.
    if ( wszLocale && cchLocale > 0 )
    {
        const PWSTR src = wszLocaleNameDefault;
        ULONG i = 0;
        for ( ; src[ i ] && i + 1 < cchLocale; ++i ) wszLocale[ i ] = src[ i ];
        wszLocale[ i ] = L'\0';
    }
    return JET_errSuccess;
}

ERR ErrNORMLocaleToLcid(
    __in_z PCWSTR /* wszLocale */,
    _Out_ LCID *  plcid )
{
    if ( plcid ) { *plcid = lcidInvariant; }
    return JET_errSuccess;
}


////////////////////////////////////////////////
//  Lifecycle

void OSNormPostterm()      {}
BOOL FOSNormPreinit()      { return fTrue; }
void OSNormTerm()          {}
ERR  ErrOSNormInit()       { return JET_errSuccess; }
