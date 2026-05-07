// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Engine-internal stubs that the OS layer (osposix) calls but that have
// no real implementation outside libese.so. Tests don't link libese.so,
// so the runners go through these no-ops.
//
// Mirrors the role of `dev/ese/src/os/litent/violated.cxx` on Windows.
// Kept TEST-LOCAL (in BstfUnitTest) so a future libese.so-linked test
// won't have a multiple-definition collision with the real engine bodies.

// We deliberately do NOT include osstd.hxx here — it pulls in the whole
// upper-OS-layer header set (cprintf.hxx, blockcache, ...) that the test
// runner doesn't compile cleanly against. Only the basic Win32 types are
// needed to match the function signatures in os/event.hxx + os/error.hxx.

#include <cc.hxx>
#include <wchar.h>
typedef wchar_t WCHAR;

class INST;
typedef INT JET_ERR;

// Mirrors of the engine's enum/typedefs from os/event.hxx. The mangled
// signature of UtilReportEvent has to match the definition the engine
// expects exactly: EEventType is an enum, Category/MessageId are DWORDs.
enum EEventType
{
    eventSuccess = 0,
    eventError = 1,
    eventWarning = 2,
    eventInformation = 4,
};
typedef DWORD CategoryId;
typedef DWORD MessageId;

void UtilReportEvent(
    const EEventType    /* type */,
    const CategoryId    /* catid */,
    const MessageId     /* msgid */,
    const DWORD         /* cString */,
    const WCHAR *       /* rgpszString */[],
    const DWORD         /* cbRawData */,
    void *              /* pvRawData */,
    const INST *        /* pinst */,
    const LONG          /* lEventLoggingLevel */ )
{
}

#ifndef MINIMAL_FUNCTIONALITY
void JetErrorToString( JET_ERR /* err */, const char ** szError, const char ** szErrorText )
{
    if ( szError )      { *szError = ""; }
    if ( szErrorText )  { *szErrorText = ""; }
}
#endif

// OS Resource Manager — engine has real bodies in cresmgr.cxx; tests
// don't need the resource manager, so trivial no-ops suffice.
typedef INT ERR;
BOOL FOSRMPreinit( void )       { return fTrue; }
ERR  ErrOSRMInit( void )        { return 0; }   // JET_errSuccess
void OSRMTerm( void )           {}
void OSRMPostterm( void )       {}
