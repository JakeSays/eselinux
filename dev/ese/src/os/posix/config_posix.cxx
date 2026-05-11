// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// POSIX equivalent of os/config.cxx.  On Windows ESE reads configuration
// from the registry under HKLM\SOFTWARE\Microsoft\<image>\...;  Linux
// has no registry, so we back the same API with two UCL config files:
//
//     /etc/ese.conf              — system-wide defaults
//     <exe-resolved>.ese.conf    — per-executable overrides
//
// Per-executable means the path is `<readlink /proc/self/exe>.ese.conf`,
// e.g. /usr/local/bin/eseutil → /usr/local/bin/eseutil.ese.conf.  Both
// files are optional; if neither exists every lookup returns "not
// found" — the engine has always tolerated that (DISABLE_REGISTRY).
//
// Schema convention:
//
//     # Categories used by FOSConfigGet_(wszPath, wszName, ...) appear
//     # as top-level objects.  Path separators ('\' or '/') walk into
//     # nested objects.  Example: FOSConfigGet_(L"BF", L"FTL Trace File")
//     # reads `BF."FTL Trace File"`.
//     BF {
//         "FTL Trace File" = "/var/log/ese-bf.ftl"
//     }
//
//     # CConfigStore opens at a path; subpaths (SysParamDefault,
//     # SysParamOverride, Diag) are nested objects underneath.
//     "Some/Store/Path" {
//         Diag {
//             SomeFlag = 1
//         }
//     }
//
// Reads are O(walk-depth); both files are parsed once on first access
// (pthread_once) and merged with per-exe winning.  Writes are no-ops —
// ESE never writes meaningful config values in the engine tree.

#include "osstd.hxx"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ucl.h"

namespace
{

//  Loaded once on first access; treated as read-only afterwards.
ucl_object_t*  g_pucMerged   = nullptr;
pthread_once_t g_uclLoadOnce = PTHREAD_ONCE_INIT;

void LoadOneFile( const char* szPath, ucl_object_t* dst )
{
    if ( access( szPath, R_OK ) != 0 )
    {
        return;
    }
    ucl_parser* parser = ucl_parser_new( UCL_PARSER_DEFAULT );
    if ( parser == nullptr )
    {
        return;
    }
    if ( ucl_parser_add_file( parser, szPath ) )
    {
        ucl_object_t* obj = ucl_parser_get_object( parser );
        if ( obj != nullptr )
        {
            (void)ucl_object_merge( dst, obj, false );
            ucl_object_unref( obj );
        }
    }
    else
    {
        const char* err = ucl_parser_get_error( parser );
        fprintf( stderr, "ese: failed to parse %s: %s\n",
                 szPath, err != nullptr ? err : "(unknown)" );
    }
    ucl_parser_free( parser );
}

void LoadConfigImpl()
{
    g_pucMerged = ucl_object_typed_new( UCL_OBJECT );
    if ( g_pucMerged == nullptr )
    {
        return;
    }

    //  Layer 1: /etc/ese.conf (system-wide).
    LoadOneFile( "/etc/ese.conf", g_pucMerged );

    //  Layer 2: <exe>.ese.conf (per-binary).
    //  /proc/self/exe is the real executable, immune to argv[0] tampering.
    char exePath[ PATH_MAX ];
    const ssize_t n = readlink( "/proc/self/exe", exePath, sizeof( exePath ) - 1 );
    if ( n > 0 && n < (ssize_t)( sizeof( exePath ) - sizeof( ".ese.conf" ) ) )
    {
        exePath[ n ] = '\0';
        char cfgPath[ PATH_MAX + 16 ];
        snprintf( cfgPath, sizeof( cfgPath ), "%s.ese.conf", exePath );
        LoadOneFile( cfgPath, g_pucMerged );
    }
}

const ucl_object_t* EnsureLoaded()
{
    pthread_once( &g_uclLoadOnce, LoadConfigImpl );
    return g_pucMerged;
}

//  Copy a WCHAR* (16-bit) ASCII string into a narrow buffer, truncating
//  on overflow.  Returns false if the source contained any non-ASCII
//  codepoint — config key names are programmer-defined and stay ASCII.
bool WideToNarrowAsciiBounded( const WCHAR* wsz, char* dst, size_t cbDst )
{
    if ( dst == nullptr || cbDst == 0 )
    {
        return false;
    }
    size_t i = 0;
    while ( wsz[ i ] != L'\0' && i + 1 < cbDst )
    {
        const unsigned int c = static_cast<unsigned int>( wsz[ i ] );
        if ( c > 0x7f )
        {
            dst[ 0 ] = '\0';
            return false;
        }
        dst[ i ] = static_cast<char>( c );
        ++i;
    }
    if ( wsz[ i ] != L'\0' )
    {
        dst[ 0 ] = '\0';
        return false;
    }
    dst[ i ] = '\0';
    return true;
}

//  Walk wszPath (using '/' or '\' as separators, registry-style) from
//  `root` and return the nested ucl object, or NULL if any segment is
//  missing.  An empty path returns `root`.
const ucl_object_t* WalkPath( const ucl_object_t* root, const WCHAR* wszPath )
{
    if ( root == nullptr || wszPath == nullptr )
    {
        return nullptr;
    }
    char narrow[ 512 ];
    if ( !WideToNarrowAsciiBounded( wszPath, narrow, sizeof( narrow ) ) )
    {
        return nullptr;
    }
    if ( narrow[ 0 ] == '\0' )
    {
        return root;
    }

    const ucl_object_t* cur = root;
    char* save = nullptr;
    for ( char* tok = strtok_r( narrow, "\\/", &save );
          tok != nullptr && cur != nullptr;
          tok = strtok_r( nullptr, "\\/", &save ) )
    {
        cur = ucl_object_lookup( cur, tok );
    }
    return cur;
}

//  Map ConfigStoreSubPath enum → the subkey name we use in the .conf
//  schema.  Matches the strings Windows' config.cxx uses for the
//  registry subkeys (m_wszSubValuePath assignments around line 455).
const char* SubpathName( ConfigStoreSubPath cssp )
{
    switch ( cssp )
    {
        case csspTop:               return "";
        case csspSysParamDefault:   return "SysParamDefault";
        case csspSysParamOverride:  return "SysParamOverride";
        case csspDiag:              return "Diag";
        default:                    return nullptr;
    }
}

const ucl_object_t* SubpathObject( const ucl_object_t* root, ConfigStoreSubPath cssp )
{
    const char* sub = SubpathName( cssp );
    if ( sub == nullptr )
    {
        return nullptr;
    }
    if ( sub[ 0 ] == '\0' )
    {
        return root;
    }
    return ucl_object_lookup( root, sub );
}

//  Pull a numeric ucl value into a 64-bit unsigned slot.  Accepts
//  integers, booleans, and numeric strings (decimal or 0x-prefixed
//  hex).  Returns errNotFound when the value is missing,
//  JET_errInvalidParameter if it's present but doesn't parse.
ERR ReadUcl64( const ucl_object_t* sub, const char* szName, QWORD* pqw )
{
    if ( sub == nullptr || szName == nullptr || pqw == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    const ucl_object_t* val = ucl_object_lookup( sub, szName );
    if ( val == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    int64_t i64 = 0;
    if ( ucl_object_toint_safe( val, &i64 ) )
    {
        *pqw = (QWORD)i64;
        return JET_errSuccess;
    }
    bool b = false;
    if ( ucl_object_toboolean_safe( val, &b ) )
    {
        *pqw = b ? 1 : 0;
        return JET_errSuccess;
    }
    const char* sz = ucl_object_tostring_forced( val );
    if ( sz != nullptr )
    {
        errno = 0;
        char* end = nullptr;
        const unsigned long long parsed =
            ( sz[ 0 ] == '0' && ( sz[ 1 ] == 'x' || sz[ 1 ] == 'X' ) )
                ? strtoull( sz, &end, 16 )
                : strtoull( sz, &end, 10 );
        if ( errno == 0 && end != sz && *end == '\0' )
        {
            *pqw = (QWORD)parsed;
            return JET_errSuccess;
        }
    }
    return ErrERRCheck( JET_errInvalidParameter );
}

}  // namespace

//  Real CConfigStore type — the public header forward-declares it; we
//  fill it with a pointer to the parsed UCL object for the root path
//  this store was opened at.  The struct is opaque to engine code.
class CConfigStore
{
public:
    const ucl_object_t* m_root;
};


const BOOL FOSConfigGet_( __in_z const WCHAR * const wszPath,
                          __in_z const WCHAR * const wszName,
                          __out_bcount_z(cbBuf) WCHAR * const wszBuf,
                          const LONG cbBuf )
{
    if ( wszBuf == nullptr || cbBuf < (LONG)sizeof( WCHAR ) )
    {
        return fFalse;
    }
    wszBuf[ 0 ] = L'\0';

    const ucl_object_t* root = EnsureLoaded();
    if ( root == nullptr )
    {
        return fFalse;
    }

    const ucl_object_t* parent = WalkPath( root, wszPath );
    if ( parent == nullptr )
    {
        return fFalse;
    }

    char narrowName[ 512 ];
    if ( !WideToNarrowAsciiBounded( wszName, narrowName, sizeof( narrowName ) ) )
    {
        return fFalse;
    }
    const ucl_object_t* val = ucl_object_lookup( parent, narrowName );
    if ( val == nullptr )
    {
        return fFalse;
    }

    const char* sz = ( ucl_object_type( val ) == UCL_STRING )
                   ? ucl_object_tostring( val )
                   : ucl_object_tostring_forced( val );
    if ( sz == nullptr )
    {
        return fFalse;
    }

    const LONG cchBuf = cbBuf / (LONG)sizeof( WCHAR );
    LONG j = 0;
    for ( ; sz[ j ] != '\0' && j + 1 < cchBuf; ++j )
    {
        wszBuf[ j ] = (WCHAR)(unsigned char)sz[ j ];
    }
    wszBuf[ j ] = L'\0';
    return fTrue;
}


ERR ErrOSConfigStoreInit( _In_z_ const WCHAR * const wszPath,
                          _Outptr_ CConfigStore ** ppcs )
{
    if ( ppcs == nullptr )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }
    *ppcs = nullptr;

    const ucl_object_t* root = EnsureLoaded();
    if ( root == nullptr )
    {
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    const ucl_object_t* storeRoot = WalkPath( root, wszPath );
    if ( storeRoot == nullptr )
    {
        return ErrERRCheck( JET_errFileNotFound );
    }

    CConfigStore* pcs = new CConfigStore();
    if ( pcs == nullptr )
    {
        return ErrERRCheck( JET_errOutOfMemory );
    }
    pcs->m_root = storeRoot;
    *ppcs = pcs;
    return JET_errSuccess;
}

void OSConfigStoreTerm( _In_ CConfigStore * pcs )
{
    delete pcs;
}


BOOL FConfigValuePresent( _In_ CConfigStore * const pcs,
                          ConfigStoreSubPath cssp,
                          _In_z_ const WCHAR * const wszValueName )
{
    if ( pcs == nullptr )
    {
        return fFalse;
    }
    const ucl_object_t* sub = SubpathObject( pcs->m_root, cssp );
    if ( sub == nullptr )
    {
        return fFalse;
    }
    char name[ 512 ];
    if ( !WideToNarrowAsciiBounded( wszValueName, name, sizeof( name ) ) )
    {
        return fFalse;
    }
    return ucl_object_lookup( sub, name ) != nullptr ? fTrue : fFalse;
}

BOOL FConfigValuePresent( _In_ CConfigStore * const pcs,
                          ConfigStoreSubPath cssp,
                          _In_z_ const CHAR * const szValueName )
{
    if ( pcs == nullptr || szValueName == nullptr )
    {
        return fFalse;
    }
    const ucl_object_t* sub = SubpathObject( pcs->m_root, cssp );
    if ( sub == nullptr )
    {
        return fFalse;
    }
    return ucl_object_lookup( sub, szValueName ) != nullptr ? fTrue : fFalse;
}


ERR ErrConfigReadValue( _In_ CConfigStore * const pcs,
                        ConfigStoreSubPath cssp,
                        _In_z_ const WCHAR * const wszValueName,
                        _Out_ QWORD * pqwValue )
{
    if ( pcs == nullptr || pqwValue == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    const ucl_object_t* sub = SubpathObject( pcs->m_root, cssp );
    if ( sub == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    char name[ 512 ];
    if ( !WideToNarrowAsciiBounded( wszValueName, name, sizeof( name ) ) )
    {
        return ErrERRCheck( errNotFound );
    }
    return ReadUcl64( sub, name, pqwValue );
}

ERR ErrConfigReadValue( _In_ CConfigStore * const pcs,
                        ConfigStoreSubPath cssp,
                        _In_z_ const WCHAR * const wszValueName,
                        _Out_ ULONG * pulValue )
{
    if ( pulValue == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    QWORD qw = 0;
    const ERR err = ErrConfigReadValue( pcs, cssp, wszValueName, &qw );
    if ( err < JET_errSuccess )
    {
        return err;
    }
    *pulValue = (ULONG)qw;
    return JET_errSuccess;
}

ERR ErrConfigReadValue( _In_ CConfigStore * const pcs,
                        ConfigStoreSubPath cssp,
                        _In_z_ const CHAR * const szValueName,
                        _Out_ QWORD * pqwValue )
{
    if ( pcs == nullptr || pqwValue == nullptr || szValueName == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    const ucl_object_t* sub = SubpathObject( pcs->m_root, cssp );
    if ( sub == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    return ReadUcl64( sub, szValueName, pqwValue );
}

ERR ErrConfigReadValue( _In_ CConfigStore * const pcs,
                        ConfigStoreSubPath cssp,
                        _In_z_ const CHAR * const szValueName,
                        _Out_ ULONG * pulValue )
{
    if ( pulValue == nullptr )
    {
        return ErrERRCheck( errNotFound );
    }
    QWORD qw = 0;
    const ERR err = ErrConfigReadValue( pcs, cssp, szValueName, &qw );
    if ( err < JET_errSuccess )
    {
        return err;
    }
    *pulValue = (ULONG)qw;
    return JET_errSuccess;
}


//  Lifecycle

void OSConfigPostterm()    {}
BOOL FOSConfigPreinit()    { return fTrue; }
void OSConfigTerm()        {}
ERR  ErrOSConfigInit()     { return JET_errSuccess; }
