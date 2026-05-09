// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Engine-side entry point for the Linux test runner. Lives inside
// eselibwithtests so it picks up the engine's PCH (std.hxx) and the
// internal types JetUnitTest::RunTests references (IFMP, ifmpNil, ...).
// The actual main() in runner_posix.cxx is a thin wrapper that just
// forwards argc/argv into RunJetUnitTestsForRunner.

#include "std.hxx"

#ifdef ENABLE_JET_UNIT_TEST

#include "jettest.hxx"

extern "C" int RunJetUnitTestsForRunner( const char* szPattern )
{
    //  RunTests("-?" / "/?") just walks the registry and prints test
    //  names; it doesn't touch engine state, so the OS-layer init is
    //  not needed and would fail on the current Linux port (eseutil
    //  hits the same "Out of memory error during OS Layer pre-init"
    //  wall). Bypass init for the listing path so we can at least
    //  verify the test registry shape.
    const bool fListing =
        ( szPattern != nullptr ) &&
        ( 0 == _stricmp( szPattern, "-?" ) || 0 == _stricmp( szPattern, "/?" ) );

    if ( !fListing )
    {
        //  std.cxx in the engine declares a process-lifetime
        //  `g_oslayerLibeseInit` whose ctor runs FOSPreinit at module load.
        //  When we link the engine statically into this exe, that ctor
        //  fires before main(), so the OS layer is already up. Construct
        //  our own COSLayerPreInit anyway so its destructor mirrors
        //  eseutil's RAII pattern, but DON'T treat !FInitd() as failure
        //  (the ctor short-circuits when g_fDllUp is already set).
        COSLayerPreInit oslayer;
        if ( !FOSDllUp() )
        {
            std::fprintf( stderr, "OS-layer pre-init failed.\n" );
            return -1;
        }
        COSLayerPreInit::DisablePerfmon();
        COSLayerPreInit::DisableTracing();
        //  RunTests itself initializes the buffer manager on demand for
        //  any test whose FNeedsBF() is true (see jettest.cxx). We don't
        //  call ErrOSInit() here because (a) the engine's static-ctor
        //  pre-init covers FOSPreinit + SetDefaults already and (b) on
        //  the current Linux port ErrOSInit returns JET_errOutOfMemory
        //  for reasons unrelated to the page-format / flushmap tests
        //  this runner is targeting. Tests that depend on the broader
        //  OS-init (oslayer_test, etc.) aren't in the portable subset
        //  yet anyway.
        const INT failures = JetUnitTest::RunTests( szPattern, ifmpNil );
        return (int)failures;
    }

    //  Listing path — no OS-layer init.
    return (int)JetUnitTest::RunTests( szPattern, ifmpNil );
}

#endif // ENABLE_JET_UNIT_TEST
