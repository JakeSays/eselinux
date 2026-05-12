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

namespace
{
inline void* HmoduleToPv(HMODULE h) { return (void*) h; }
inline HMODULE PvToHmodule(void* p) { return (HMODULE) p; }
} // anonymous

BOOL FUtilLoadLibrary(const WCHAR* wszLibrary, LIBRARY* plibrary, const BOOL /* fPermitDialog */)
{
    *plibrary = 0;

    if (wszLibrary == nullptr)
    {
        return fFalse;
    }

    char szLibrary[1024];
    size_t i = 0;
    for (; wszLibrary[i] != L'\0' && i + 1 < sizeof(szLibrary); ++i)
    {
        szLibrary[i] = (char) (wszLibrary[i] & 0xFF);
    }
    szLibrary[i] = '\0';

    void* p = dlopen(szLibrary, RTLD_NOW | RTLD_LOCAL);
    if (p == nullptr)
    {
        return fFalse;
    }

    *plibrary = (LIBRARY) p;
    return fTrue;
}

PFN PfnUtilGetProcAddress(LIBRARY library, const char* szFunction)
{
    if (!library || !szFunction)
    {
        return nullptr;
    }
    return (PFN) dlsym((void*) library, szFunction);
}

void UtilFreeLibrary(LIBRARY library)
{
    if (library)
    {
        dlclose((void*) library);
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

VOID OSLibraryValidateLoaderPolicy(const WCHAR* const /* mwszzDlls */, OSLoadFlags /* oslf */)
{
}

VOID OSLibraryTrackingLoad(const WCHAR* const /* mwszzDlls */)
{
}

VOID OSLibraryTrackingFree(const WCHAR* const /* mwszzDlls */)
{
}

#endif // DEBUG

ERR ErrMultiLoadPfn(
    const WCHAR* const /* mwszzDlls */,
    const BOOL /* fNonSystemDll */,
    const CHAR* const szFunction,
    SHORT* const pichDll,
    void** const ppfn)
{
    // Linux has no Windows system DLLs to LoadLibrary, but the windows-shim
    // implements many of the API entry points the engine expects (Create-
    // ThreadpoolTimer, RegOpenKeyExW, ...). Look the function up in our
    // own process image via dlsym(RTLD_DEFAULT, ...). Anything not shimmed
    // returns JET_errUnloadableOSFunctionality, which the engine's
    // FunctionLoader fall-back paths handle.
    if (pichDll)
    {
        *pichDll = -1;
    }
    if (ppfn)
    {
        *ppfn = nullptr;
    }
    if (!szFunction || !*szFunction)
    {
        return JET_errUnloadableOSFunctionality;
    }
    void* const sym = dlsym(RTLD_DEFAULT, szFunction);
    if (sym)
    {
        if (ppfn)
            *ppfn = sym;
        if (pichDll)
            *pichDll = 0;
        return JET_errSuccess;
    }
    extern ERR g_errTrap;
    if (g_fDllUp)
    {
        return ErrERRCheck_(JET_errUnloadableOSFunctionality, __FILE__, __LINE__);
    }
    Assert(g_errTrap != JET_errUnloadableOSFunctionality);
    return JET_errUnloadableOSFunctionality;
}

VOID FreeLoadedModule(const WCHAR* const /* wszDll */)
{
}

void OSLibraryPostterm()
{
}

BOOL FOSLibraryPreinit() { return fTrue; }

void OSLibraryTerm()
{
}

ERR ErrOSLibraryInit() { return JET_errSuccess; }
