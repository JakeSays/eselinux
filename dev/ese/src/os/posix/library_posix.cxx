// POSIX equivalent of library.cxx. Wraps the LIBRARY/PFN/HMODULE-style
// surface used by ESE's user callback DLL loading, plus the ErrMultiLoadPfn
// machinery that backs the NTOSFunc* delay-load wrappers in the engine.
//
// User-supplied callback libraries go through dlopen/dlsym; everything else
// (the entire NTOSFunc system DLL surface) is stubbed to fail with
// JET_errUnloadableOSFunctionality so that the engine's "function not
// available" fall-back paths kick in for Windows-only OS APIs.

#include "osstd.hxx"

#include <dlfcn.h>

extern volatile BOOL g_fDllUp;

namespace {

inline void* HmoduleToPv( HMODULE h ) { return (void*)h; }
inline HMODULE PvToHmodule( void* p ) { return (HMODULE)p; }

} // anonymous

BOOL FUtilLoadLibrary( const WCHAR* wszLibrary, LIBRARY* plibrary, const BOOL /* fPermitDialog */ )
{
    *plibrary = NULL;

    if ( wszLibrary == NULL )
    {
        return fFalse;
    }

    char szLibrary[ 1024 ];
    size_t i = 0;
    for ( ; wszLibrary[i] != L'\0' && i + 1 < sizeof( szLibrary ); ++i )
    {
        szLibrary[i] = (char)( wszLibrary[i] & 0xFF );
    }
    szLibrary[i] = '\0';

    void* p = dlopen( szLibrary, RTLD_NOW | RTLD_LOCAL );
    if ( p == NULL )
    {
        return fFalse;
    }

    *plibrary = (LIBRARY)p;
    return fTrue;
}

PFN PfnUtilGetProcAddress( LIBRARY library, const char* szFunction )
{
    if ( !library || !szFunction )
    {
        return NULL;
    }
    return (PFN)dlsym( (void*)library, szFunction );
}

void UtilFreeLibrary( LIBRARY library )
{
    if ( library )
    {
        dlclose( (void*)library );
    }
}

#define STATUS_NOT_IMPLEMENTED  ((INT)0xC0000002L)

INT NtstatusThunkNotSupported()
{
    return STATUS_NOT_IMPLEMENTED;
}

DWORD ErrorThunkNotSupported()
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

#ifdef DEBUG

VOID OSLibraryValidateLoaderPolicy( const WCHAR * const /* mwszzDlls */, OSLoadFlags /* oslf */ )
{
}

VOID OSLibraryTrackingLoad( const WCHAR * const /* mwszzDlls */ )
{
}

VOID OSLibraryTrackingFree( const WCHAR * const /* mwszzDlls */ )
{
}

#endif // DEBUG

ERR ErrMultiLoadPfn(
    const WCHAR * const /* mwszzDlls */,
    const BOOL          /* fNonSystemDll */,
    const CHAR * const  /* szFunction */,
    SHORT * const       pichDll,
    void ** const       ppfn )
{
    // Linux has no equivalent of these Windows system DLLs (kernel32,
    // ntdll, advapi32, etc.). Always return "unloadable" so the engine
    // falls back through pfn->ErrIsPresent() < JET_errSuccess paths.
    if ( pichDll )
    {
        *pichDll = -1;
    }
    if ( ppfn )
    {
        *ppfn = NULL;
    }
    extern ERR g_errTrap;
    if ( g_fDllUp )
    {
        return ErrERRCheck_( JET_errUnloadableOSFunctionality, __FILE__, __LINE__ );
    }
    Assert( g_errTrap != JET_errUnloadableOSFunctionality );
    return JET_errUnloadableOSFunctionality;
}

VOID FreeLoadedModule( const WCHAR * const /* wszDll */ )
{
}

void OSLibraryPostterm()       {}
BOOL FOSLibraryPreinit()       { return fTrue; }
void OSLibraryTerm()           {}
ERR  ErrOSLibraryInit()        { return JET_errSuccess; }
