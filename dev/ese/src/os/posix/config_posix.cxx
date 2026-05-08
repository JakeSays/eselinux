// POSIX equivalent of os/config.cxx. The Linux build has no Windows registry,
// so FOSConfigGet_ behaves like the upstream DISABLE_REGISTRY path (returns
// fFalse, blanks the buffer) and the V2 CConfigStore APIs short-circuit to
// JET_errFeatureNotAvailable. The CConfigStore type is opaque to clients —
// it's only ever a forward-declared pointer in the engine — so we don't need
// to carry the original class definition.

#include "osstd.hxx"

class CConfigStore
{
};


const BOOL FOSConfigGet_( __in_z const WCHAR * const /* wszPath */,
                          __in_z const WCHAR * const /* wszName */,
                          __out_bcount_z(cbBuf) WCHAR * const wszBuf,
                          const LONG cbBuf )
{
    if ( cbBuf >= (LONG)sizeof( WCHAR ) )
    {
        wszBuf[0] = L'\0';
    }
    return fFalse;
}


ERR ErrOSConfigStoreInit( _In_z_ const WCHAR * const /* wszPath */,
                          _Outptr_ CConfigStore ** ppcs )
{
    *ppcs = nullptr;
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

void OSConfigStoreTerm( _In_ CConfigStore * /* pcs */ )
{
    //  nop — ErrOSConfigStoreInit always fails, so pcs is always NULL here
}


BOOL FConfigValuePresent( _In_ CConfigStore * const /* pcs */,
                          ConfigStoreSubPath /* cssp */,
                          _In_z_ const WCHAR * const /* wszValueName */ )
{
    return fFalse;
}

BOOL FConfigValuePresent( _In_ CConfigStore * const /* pcs */,
                          ConfigStoreSubPath /* cssp */,
                          _In_z_ const CHAR * const /* szValueName */ )
{
    return fFalse;
}

ERR ErrConfigReadValue( _In_ CConfigStore * const /* pcs */,
                        ConfigStoreSubPath /* cssp */,
                        _In_z_ const WCHAR * const /* wszValueName */,
                        _Out_ QWORD * /* pqwValue */ )
{
    return ErrERRCheck( errNotFound );
}

ERR ErrConfigReadValue( _In_ CConfigStore * const /* pcs */,
                        ConfigStoreSubPath /* cssp */,
                        _In_z_ const WCHAR * const /* wszValueName */,
                        _Out_ ULONG * /* pulValue */ )
{
    return ErrERRCheck( errNotFound );
}

ERR ErrConfigReadValue( _In_ CConfigStore * const /* pcs */,
                        ConfigStoreSubPath /* cssp */,
                        _In_z_ const CHAR * const /* szValueName */,
                        _Out_ QWORD * /* pqwValue */ )
{
    return ErrERRCheck( errNotFound );
}

ERR ErrConfigReadValue( _In_ CConfigStore * const /* pcs */,
                        ConfigStoreSubPath /* cssp */,
                        _In_z_ const CHAR * const /* szValueName */,
                        _Out_ ULONG * /* pulValue */ )
{
    return ErrERRCheck( errNotFound );
}


//  Lifecycle

void OSConfigPostterm()    {}
BOOL FOSConfigPreinit()    { return fTrue; }
void OSConfigTerm()        {}
ERR  ErrOSConfigInit()     { return JET_errSuccess; }
