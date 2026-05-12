// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Engine-side entry point for the Linux test runner.  Lives inside
// eselibwithtests so it picks up the engine's PCH (std.hxx) and the
// internal types JetUnitTest::RunTests references (IFMP, ifmpNil, ...).
// The actual main() in runner_posix.cxx is a thin wrapper that just
// forwards argv into RunJetUnitTestsForRunner.
//
// Two modes:
//
//   * Tier 1 — no -d argument.  Bring up only the OS layer (ErrOSInit),
//     run RunTests with ifmpNil.  Every JETUNITTESTDB-declared test is
//     skipped because IfmpTest() would be nil.
//
//   * Tier 2 — `-d <dir>` argument.  Skip ErrOSInit and let JetInit own
//     the OS layer.  Create a fresh database under <dir>, derive its
//     IFMP via the engine's dbid→ifmp map, hand that to RunTests so the
//     JETUNITTESTDB bodies see a real open database.

#include "std.hxx"

#ifdef ENABLE_JET_UNIT_TEST

#include "jettest.hxx"

#include <cstdio>
#include <cstring>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace
{

//  Test runs benefit from full event-log visibility — both information
//  events and the highest gated trace level.  Set both globally
//  (pinst=NULL) so they apply to every JET instance the runner creates
//  and to the OSU-layer paths the tier-1 runner exercises without a
//  real JET instance.  The engine defaults already produce these
//  values, but the test exes JET_paramConfigStoreSpec, sysparamtable
//  overrides, or future changes could move them — set explicitly so
//  the runner's behaviour stays pinned regardless.
void EnableEventLogging()
{
    JET_ERR err = JET_errSuccess;
    err = JetSetSystemParameterA( nullptr, 0, JET_paramNoInformationEvent, 0, nullptr );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(NoInformationEvent) failed: %d\n", (int)err );
    }
    err = JetSetSystemParameterA( nullptr, 0, JET_paramEventLoggingLevel, JET_EventLoggingLevelMax, nullptr );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(EventLoggingLevel) failed: %d\n", (int)err );
    }
}

int RunTier1( const char* szPattern )
{
    //  std.cxx in the engine declares a process-lifetime
    //  `g_oslayerLibeseInit` whose ctor runs FOSPreinit at module load.
    //  When we link the engine statically into this exe, that ctor
    //  fires before main(), so the OS layer is already up.  Construct
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
    //  Register the engine TLS size up front so Ptls() is usable in
    //  any code path the tests exercise.  ErrOSUSetOSLayerGlobals()
    //  (invoked from ErrOSUInit below) also does this, but a few of
    //  the early COSLayerPreInit hooks may already need the size.
    OSPrepreinitSetUserTLSSize( sizeof( TLS ) );
    //  Perfmon isn't compiled into the Linux OS layer (osposix omits
    //  PERFMON_SUPPORT).  The COSLayerPreInit::DisablePerfmon() shortcut
    //  above sets the global flag but ErrOSUInit consults the JET system
    //  parameter, which defaults to "enabled" — so we also have to set
    //  the param explicitly or ErrOSUInit will call EnablePerfmon which
    //  asserts in perfmon.cxx:31 with "No perfmon support enabled".
    JET_ERR errParam = JetSetSystemParameterA( nullptr, 0, JET_paramDisablePerfmon, 1, nullptr );
    if ( errParam < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(DisablePerfmon) failed: %d\n", (int)errParam );
        return -1;
    }
    //  The fuzz tests in node_test (TestCorrupt*FullFuzz*) deliberately
    //  trigger engine AssertTracks on corrupted pages and rely on the
    //  default Windows JET_paramAssertAction allowing them to continue.
    //  The Linux default is JET_AssertFailFast which aborts the process
    //  on the first internal assert.  Switch to SkipAll so engine asserts
    //  are still recorded but don't terminate — the per-test CHECK chain
    //  still detects real failures via JetUnitTestResult.
    errParam = JetSetSystemParameterA( nullptr, 0, JET_paramAssertAction, JET_AssertSkipAll, nullptr );
    if ( errParam < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(AssertAction) failed: %d\n", (int)errParam );
        return -1;
    }
    //  Bring up the FULL OSU stack (vs the lower-level ErrOSInit we
    //  used previously) so g_OSUInitControl's consumer count is bumped.
    //  That matters for tier-1 tests that themselves call JetInit/JetInit2
    //  (e.g. FMP.NewAndWriteLatch): their nested ErrOSUInit takes the
    //  CInitTermLock early-return path instead of trying to re-SetParam
    //  the already-frozen CResourceManager globals
    //  (cresmgr.cxx:648 -> JET_errAlreadyInitialized via osu.cxx:473).
    const ERR errInit = ErrOSUInit();
    if ( errInit < JET_errSuccess )
    {
        std::fprintf( stderr, "ErrOSUInit failed: %d\n", (int)errInit );
        return -1;
    }
    EnableEventLogging();
    const INT failures = JetUnitTest::RunTests( szPattern, ifmpNil );
    OSUTerm();
    return (int)failures;
}

//  Set a string-valued system parameter and abort the run with a
//  printed error on failure.  pinstance must be the same handle that
//  later gets passed to JetInit.
JET_ERR SetSysParamSz( JET_INSTANCE* pinstance, ULONG paramid, const char* szValue, const char* szName )
{
    const JET_ERR err = JetSetSystemParameterA( pinstance, 0, paramid, 0, szValue );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(%s=%s) failed: %d\n",
                      szName, szValue, (int)err );
    }
    return err;
}

int RunTier2( const char* szPattern, const char* szDbDir )
{
    JET_INSTANCE instance = JET_instanceNil;
    JET_SESID    sesid    = JET_sesidNil;
    JET_DBID     dbid     = JET_dbidNil;

    //  ErrOSUSetOSLayerGlobals (called from JetCreateInstance via the
    //  first JetInit) is what would normally register the TLS size.  But
    //  JetSetSystemParameter touches Ptls() before that runs, so wire it
    //  up directly — the call is idempotent.
    OSPrepreinitSetUserTLSSize( sizeof( TLS ) );

    //  Resolve all engine files (system, logs, temp DB) under the user-
    //  supplied directory.  Trailing slash matters — JET concatenates
    //  the path with bare file names internally.
    char szDir[ 4096 ];
    size_t cchDir = std::strlen( szDbDir );
    if ( cchDir + 2 > sizeof( szDir ) )
    {
        std::fprintf( stderr, "-d path too long: %s\n", szDbDir );
        return -1;
    }
    std::memcpy( szDir, szDbDir, cchDir );
    if ( cchDir == 0 || szDir[ cchDir - 1 ] != '/' )
    {
        szDir[ cchDir++ ] = '/';
    }
    szDir[ cchDir ] = '\0';

    //  Create the directory if it doesn't already exist.  Tolerate EEXIST
    //  (the user supplied a path that already has prior runner state).
    //  Anything else — including a missing parent — is fatal here so the
    //  user gets a clear errno rather than a downstream JET_errFileAccessDenied
    //  from ErrOpenTempLogFile.
    if ( mkdir( szDbDir, 0755 ) != 0 && errno != EEXIST )
    {
        std::fprintf( stderr, "mkdir(%s) failed: %s\n", szDbDir, std::strerror( errno ) );
        return -1;
    }

    JET_ERR err = JET_errSuccess;
    err = SetSysParamSz( &instance, JET_paramSystemPath, szDir, "SystemPath" );
    if ( err < JET_errSuccess )
    {
        return -1;
    }
    err = SetSysParamSz( &instance, JET_paramTempPath, szDir, "TempPath" );
    if ( err < JET_errSuccess )
    {
        return -1;
    }
    err = SetSysParamSz( &instance, JET_paramLogFilePath, szDir, "LogFilePath" );
    if ( err < JET_errSuccess )
    {
        return -1;
    }
    err = SetSysParamSz( &instance, JET_paramBaseName, "edb", "BaseName" );
    if ( err < JET_errSuccess )
    {
        return -1;
    }
    err = SetSysParamSz( &instance, JET_paramEventSource, "EseLibWithTestsRunner", "EventSource" );
    if ( err < JET_errSuccess )
    {
        return -1;
    }
    //  CircularLog=1 keeps the log directory bounded — the runner is a
    //  one-shot process so we don't care about replaying past it.
    err = JetSetSystemParameterA( &instance, 0, JET_paramCircularLog, 1, nullptr );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(CircularLog) failed: %d\n", (int)err );
        return -1;
    }

    //  Perfmon is stubbed out in the Linux port (PERFMON_SUPPORT off).
    //  Tell JET so ErrOSUSetOSLayerGlobals doesn't try to enable it.
    err = JetSetSystemParameterA( &instance, 0, JET_paramDisablePerfmon, 1, nullptr );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(DisablePerfmon) failed: %d\n", (int)err );
        return -1;
    }
    //  Tier-1 sets AssertSkipAll for the same reason — FireWalls fire on
    //  recoverable engine quirks (e.g. ZFS reports 128K block size, engine
    //  clamps to 4K and continues), and Debug's AssertFailFast would
    //  otherwise abort the runner before any test ran.
    err = JetSetSystemParameterA( &instance, 0, JET_paramAssertAction, JET_AssertSkipAll, nullptr );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetSetSystemParameter(AssertAction) failed: %d\n", (int)err );
        return -1;
    }
    //  Pin info events and full event-log level on, applied to every
    //  JET instance created in this process.
    EnableEventLogging();

    err = JetInit( &instance );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetInit failed: %d\n", (int)err );
        return -1;
    }

    err = JetBeginSessionA( instance, &sesid, nullptr, nullptr );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetBeginSession failed: %d\n", (int)err );
        JetTerm( instance );
        return -1;
    }

    char szDbPath[ 4096 ];
    if ( std::snprintf( szDbPath, sizeof( szDbPath ), "%stest.edb", szDir )
            >= (int)sizeof( szDbPath ) )
    {
        std::fprintf( stderr, "test db path overflow\n" );
        JetEndSession( sesid, 0 );
        JetTerm( instance );
        return -1;
    }
    err = JetCreateDatabaseA( sesid, szDbPath, nullptr, &dbid, JET_bitDbOverwriteExisting );
    if ( err < JET_errSuccess )
    {
        std::fprintf( stderr, "JetCreateDatabase(%s) failed: %d\n", szDbPath, (int)err );
        JetEndSession( sesid, 0 );
        JetTerm( instance );
        return -1;
    }

    //  Translate JET_DBID → IFMP via the engine's per-instance dbid map.
    //  JetSimpleDbUnitTest::SetTestIfmp / IfmpTest() expects an IFMP, not
    //  a dbid — those are different namespaces (a dbid is per-session;
    //  an IFMP indexes the global FMP table).
    const PIB*  ppib  = PpibFromSesid( sesid );
    const INST* pinst = PinstFromPpib( ppib );
    const IFMP  ifmp  = pinst->m_mpdbidifmp[ (DBID)dbid ];

    const INT failures = JetUnitTest::RunTests( szPattern, ifmp );

    (void)JetCloseDatabase( sesid, dbid, 0 );
    (void)JetEndSession( sesid, 0 );
    (void)JetTerm( instance );
    return (int)failures;
}

}  // namespace

extern "C" int RunJetUnitTestsForRunner( const char* szPattern, const char* szDbDir )
{
    //  Listing path ("-?" / "/?") just walks the registry and prints
    //  names; no engine state is touched.  Bypass all init.
    const bool fListing =
        ( szPattern != nullptr ) &&
        ( 0 == _stricmp( szPattern, "-?" ) || 0 == _stricmp( szPattern, "/?" ) );

    if ( fListing )
    {
        return (int)JetUnitTest::RunTests( szPattern, ifmpNil );
    }

    if ( szDbDir != nullptr )
    {
        return RunTier2( szPattern, szDbDir );
    }
    return RunTier1( szPattern );
}

#endif // ENABLE_JET_UNIT_TEST
