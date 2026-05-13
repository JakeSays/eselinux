// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _OS_CONFIG_HXX_INCLUDED
#define _OS_CONFIG_HXX_INCLUDED

//  config.hxx ships both at the source path (dev/ese/published/inc/os/)
//  and copied to ${INC_OUTPUT_DIRECTORY}/config.hxx by CMake.  A plain
//  `#include "platform.h"` works from either copy: every consumer of
//  the published headers has dev/ese/published/inc/ on its -I path,
//  so the search lands on the canonical platform.h there.
#include "platform.h"   //  ESE_OS_WINDOWS / ESE_OS_LINUX


//  On Windows, RTM/MINIMAL_FUNCTIONALITY builds suppress all registry
//  reads (hardening: production callers shouldn't be redirected via
//  HKLM\Software\Microsoft\<image>\... at runtime).  Linux mirrors
//  the same surface with a deliberate user-visible config file
//  (/etc/ese.conf + <exe>.ese.conf), which is the documented way to
//  configure the engine — so the gate does NOT apply on Linux.  RTM
//  Linux builds still honour the .ese.conf files.
#if defined(ESE_OS_WINDOWS) && ( defined(RTM) || defined(MINIMAL_FUNCTIONALITY) )
#ifndef DEBUG
#define DISABLE_REGISTRY
#endif
#endif

#ifdef DISABLE_REGISTRY

#define FOSConfigGet( wszPath, wszName, wszBuf, cbBuf ) ( fFalse )

#else  //  !DISABLE_REGISTRY

#define FOSConfigGet( wszPath, wszName, wszBuf, cbBuf ) FOSConfigGet_( wszPath, wszName, wszBuf, cbBuf )

#endif  //  DISABLE_REGISTRY


//  Persistent Configuration Management
//
//  Configuration information is organized in a way very similar to a file
//  system.  Each configuration datum is a name and value pair stored in a
//  path.  All paths, names, ans values are text strings.

//  gets the value of the specified name in the specified path and places it in
//  the provided buffer, returning fFalse if the buffer is too small.  if the
//  value is not set, an empty string will be returned
//
//  NOTE:  either '/' or '\\' is a valid path separator

const BOOL FOSConfigGet_( __in_z const WCHAR * const wszPath, __in_z const WCHAR* const wszName, __out_bcount_z(cbBuf) WCHAR* const wszBuf, const LONG cbBuf );


//  Linux-only: select the config-file path the next ErrOSConfigInit
//  will read.  Pass nullptr (or empty) to fall back to the layered
//  default (/etc/ese.conf + <exe>.ese.conf).  No-op once the config
//  has been loaded; the caller (JetPlatformInitialize2) is expected
//  to call this strictly before ErrOSConfigInit runs.
//
//  Windows config.cxx reads from the registry live — there's no
//  equivalent surface there, and the stub below makes that explicit.
#ifdef ESE_OS_LINUX
void OSConfigSetPath( const char * szPath );
#else
inline void OSConfigSetPath( const char * /*szPath*/ ) { }
#endif


//  V2 of Persistent Configuration
//  

class CConfigStore;

ERR ErrOSConfigStoreInit( _In_z_ const WCHAR * const wszPath, _Outptr_ CConfigStore ** ppcs );
void OSConfigStoreTerm( _In_ CConfigStore * pcs );

// It is not clear if we should mark these as PERSISTED ... they ARE persisted by clients
// to communicate with use via the registry.
enum ConfigStoreReadFlags   //  csrf
{
    //  These values are all specified in the JET header, such as:
    //  JET_bitConfigStoreReadControlInhibitRead, JET_bitConfigStoreReadControlDisableAll, etc
};

enum ConfigStoreSubPath //  cssp
{
    csspTop = 0,

    csspSysParamDefault,
    csspSysParamOverride,

    csspDiag,

    //  Adding a sub-path requires also updating the list of initialized m_wszSubValuePath
    //  entries in CConfigStore::CConfigStore.


    csspMax,
};

//  To retrieve / read a value from the ConfigStore.
//
BOOL FConfigValuePresent( _In_ CConfigStore * const pcs, ConfigStoreSubPath cssp, _In_z_ const WCHAR * const wszValueName );
BOOL FConfigValuePresent( _In_ CConfigStore * const pcs, ConfigStoreSubPath cssp, _In_z_ const CHAR * const szValueName );

ERR ErrConfigReadValue( _In_ CConfigStore * const pcs, ConfigStoreSubPath cssp, _In_z_ const CHAR * const szValueName, _Out_ ULONG * pulValue );
ERR ErrConfigReadValue( _In_ CConfigStore * const pcs, ConfigStoreSubPath cssp, _In_z_ const WCHAR * const wszValueName, _Out_ ULONG * pulValue );

ERR ErrConfigReadValue( _In_ CConfigStore * const pcs, ConfigStoreSubPath cssp, _In_z_ const CHAR * const szValueName, _Out_ QWORD * pqwValue );
ERR ErrConfigReadValue( _In_ CConfigStore * const pcs, ConfigStoreSubPath cssp, _In_z_ const WCHAR * const wszValueName, _Out_ QWORD * pqwValue );

#endif  //  _OS_CONFIG_HXX_INCLUDED

