// POSIX equivalent of dllentry.cxx. Linux .so init/term happens via ctor/
// dtor attributes (no DLL_PROCESS_ATTACH/DETACH). This file owns the same
// globals the Win32 entry point sets so the rest of the engine sees the
// same surface — FOSDllUp(), g_fDllUp, g_fProcessExit, g_tidDLLEntryPoint.
//
// COSLayerPreInit (in os.cxx) drives FOSPreinit/OSPostterm, so we don't
// need a true ctor entry on Linux; library-challenged binaries (like
// eseutil) construct it directly. The destructor attribute below only
// records that we're tearing down due to process exit.

#include "osstd.hxx"

#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>

extern "C" {

BOOL            g_fProcessExit = fFalse;
volatile BOOL   g_fDllUp = fFalse;
volatile DWORD  g_tidDLLEntryPoint = 0;

} // extern "C"

BOOL FOSDllUp()
{
    return g_fDllUp;
}

extern VOID OSEventRegister();

HRESULT DllRegisterServer()
{
    OSEventRegister();
    return S_OK;
}

namespace {

__attribute__((destructor))
void EsePosixSoFini()
{
    g_fProcessExit = fTrue;
}

} // namespace
