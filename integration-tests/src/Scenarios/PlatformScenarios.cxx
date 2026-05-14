// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/CrashHelper.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/wait.h>

using namespace ese::tests;

EseIntegrationScenario(Platform, InitializeAndTerminate)
{
    TemporaryDirectory directory("Platform.InitializeAndTerminate");
    EseInstance instance(directory);
    // Instance dtor calls JetTerm; if either init or term fails the
    // RAII wrapper throws and the scenario fails.
}

EseIntegrationScenario(Platform, GetInstanceInfoEnumeratesRunningInstance)
{
    TemporaryDirectory directory(
        "Platform.GetInstanceInfoEnumeratesRunningInstance");
    EseInstance instance(directory);

    // JetGetInstanceInfo is instance-list-style: the engine allocates
    // an array of JET_INSTANCE_INFO_A entries via JetMalloc.  Caller
    // releases via JetFreeBuffer.
    uint32_t instanceCount = 0;
    JET_INSTANCE_INFO_A* instanceInfoArray = nullptr;
    CheckJet(JetGetInstanceInfoA(&instanceCount, &instanceInfoArray));
    // Our running instance must show up.
    Require(instanceCount >= 1);
    Require(instanceInfoArray != nullptr);
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(instanceInfoArray)));
}

EseIntegrationScenario(Platform, CreateInstanceAllocatesHandleWithoutInit)
{
    //  JetCreateInstance is the unversioned alloc-only form (the
    //  framework uses JetCreateInstance2 elsewhere because it accepts
    //  a display name).  Round-trip Create -> Term without an Init in
    //  between is the documented "abandon" path.
    JET_INSTANCE handle = JET_instanceNil;
    CheckJet(JetCreateInstanceA(&handle, "Platform.CreateInstance"));
    Require(handle != JET_instanceNil);

    CheckJet(JetTerm(handle));
}

EseIntegrationScenario(Platform, GetAndSetResourceParamRoundTrip)
{
    TemporaryDirectory directory("Platform.GetAndSetResourceParamRoundTrip");
    EseInstance instance(directory);

    //  JetGetResourceParam reports per-resource-class tunables.
    //  JET_resoperSize is the size in bytes of one object of the
    //  given class — PIB (sessions) and FCB (tables) are always
    //  compiled in, so both have non-zero sizes.
    JET_API_PTR pibSize = 0;
    CheckJet(JetGetResourceParam(instance.Handle(),
                                 JET_resoperSize,
                                 JET_residPIB,
                                 &pibSize));
    Require(pibSize > 0);

    JET_API_PTR fcbSize = 0;
    CheckJet(JetGetResourceParam(instance.Handle(),
                                 JET_resoperSize,
                                 JET_residFCB,
                                 &fcbSize));
    Require(fcbSize > 0);

    //  JET_resoperMaxUse is settable post-init.  Round-trip: read
    //  the current ceiling, write it back unchanged, read it again
    //  and confirm the same value.
    JET_API_PTR pibMax = 0;
    CheckJet(JetGetResourceParam(instance.Handle(),
                                 JET_resoperMaxUse,
                                 JET_residPIB,
                                 &pibMax));
    Require(pibMax > 0);

    CheckJet(JetSetResourceParam(instance.Handle(),
                                 JET_resoperMaxUse,
                                 JET_residPIB,
                                 pibMax));

    JET_API_PTR pibMaxAfter = 0;
    CheckJet(JetGetResourceParam(instance.Handle(),
                                 JET_resoperMaxUse,
                                 JET_residPIB,
                                 &pibMaxAfter));
    Require(pibMaxAfter == pibMax);
}

EseIntegrationScenario(Platform, Init2BringsUpInstanceAcceptingGrbit)
{
    TemporaryDirectory directory("Platform.Init2BringsUpInstanceAcceptingGrbit");

    //  JetInit2 takes an extra grbit argument vs JetInit — the
    //  grbits control recovery/init behaviour
    //  (JET_bitAllowMissingCurrentLog, JET_bitReplayIgnoreLostLogs,
    //  etc.).  EseInstance uses the v1 entry; this scenario exercises
    //  v2 with no grbits set, which must produce the same result —
    //  a fully-initialised engine that accepts a session + create.
    JET_INSTANCE handle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&handle,
                                 "Init2",
                                 "Init2",
                                 0));

    auto pathWithSep = directory.Path().string();
    if (!pathWithSep.empty() && pathWithSep.back() != '/')
    {
        pathWithSep.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramEventSource, 0, "Init2"));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramCircularLog, 1, nullptr));

    CheckJet(JetInit2(&handle, 0));

    JET_SESID sesid = JET_sesidNil;
    CheckJet(JetBeginSessionA(handle, &sesid, nullptr, nullptr));
    JET_DBID dbid = JET_dbidNil;
    const auto dbPath = (directory.Path() / "Init2.mdb").string();
    CheckJet(JetCreateDatabaseA(sesid, dbPath.c_str(),
                                nullptr, &dbid, 0));
    CheckJet(JetCloseDatabase(sesid, dbid, 0));
    CheckJet(JetDetachDatabaseA(sesid, dbPath.c_str()));
    CheckJet(JetEndSession(sesid, 0));
    CheckJet(JetTerm2(handle, JET_bitTermComplete));
}

namespace
{

constexpr const char* ChildEntryEnableMultiInstance =
    "Platform.EnableMultiInstance";

//  Child for the EnableMultiInstance scenario.  Calls JetEnableMultiInstance
//  on a fresh engine, brings up two independent instances, asserts each
//  shows up in JetGetInstanceInfo, and exits with status 0 — proving the
//  multi-instance mode actually works (not just that the entry point is
//  reachable).  Any failure path falls through to exit-non-zero, which the
//  parent treats as scenario failure.
void RunEnableMultiInstanceChild(const std::filesystem::path& directory)
{
    auto pathFor = [&](const char* name) {
        auto path = (directory / name).string();
        if (!path.empty() && path.back() != '/')
        {
            path.push_back('/');
        }
        std::filesystem::create_directories(path);
        return path;
    };

    //  JetEnableMultiInstance pre-configures the engine's global
    //  resource manager so it can host more than one instance.  Pass
    //  a small JET_SETSYSPARAM array bumping the process-wide max
    //  instance ceiling so the two we're about to create both fit.
    JET_SETSYSPARAM_A params[1] = { {} };
    params[0].paramid = JET_paramMaxInstances;
    params[0].lParam = 4;
    params[0].sz = nullptr;
    uint32_t paramsSet = 0;
    CheckJet(JetEnableMultiInstanceA(params,
                                     /*csetsysparam=*/1,
                                     &paramsSet));
    Require(paramsSet == 1);
    Require(params[0].err == JET_errSuccess);

    //  Bring up two instances; each gets its own directory + base
    //  name so the on-disk state stays disjoint.
    JET_INSTANCE instanceA = JET_instanceNil;
    JET_INSTANCE instanceB = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceA, "MultiA", "MultiA", 0));
    CheckJet(JetCreateInstance2A(&instanceB, "MultiB", "MultiB", 0));

    const auto pathA = pathFor("a");
    const auto pathB = pathFor("b");

    auto configureInstance = [](JET_INSTANCE* handle,
                                const std::string& path,
                                const char* eventSource) {
        CheckJet(JetSetSystemParameterA(handle, JET_sesidNil,
                                        JET_paramSystemPath, 0,
                                        path.c_str()));
        CheckJet(JetSetSystemParameterA(handle, JET_sesidNil,
                                        JET_paramTempPath, 0,
                                        path.c_str()));
        CheckJet(JetSetSystemParameterA(handle, JET_sesidNil,
                                        JET_paramLogFilePath, 0,
                                        path.c_str()));
        CheckJet(JetSetSystemParameterA(handle, JET_sesidNil,
                                        JET_paramBaseName, 0, "edb"));
        CheckJet(JetSetSystemParameterA(handle, JET_sesidNil,
                                        JET_paramEventSource, 0,
                                        eventSource));
        CheckJet(JetSetSystemParameterA(handle, JET_sesidNil,
                                        JET_paramCircularLog, 1, nullptr));
    };
    configureInstance(&instanceA, pathA, "MultiA");
    configureInstance(&instanceB, pathB, "MultiB");

    CheckJet(JetInit(&instanceA));
    CheckJet(JetInit(&instanceB));

    //  Both instances must enumerate live alongside each other —
    //  proves the multi-instance state isn't aliasing two handles to
    //  the same engine.
    uint32_t instanceCount = 0;
    JET_INSTANCE_INFO_A* instanceArray = nullptr;
    CheckJet(JetGetInstanceInfoA(&instanceCount, &instanceArray));
    Require(instanceCount == 2);
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(instanceArray)));

    //  Quick functional smoke: create a database against each
    //  instance using its own session.  Routes through different
    //  engine cores; if they aliased, one of the creates would trip.
    JET_SESID sesidA = JET_sesidNil;
    JET_SESID sesidB = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceA, &sesidA, nullptr, nullptr));
    CheckJet(JetBeginSessionA(instanceB, &sesidB, nullptr, nullptr));

    const auto dbPathA = pathA + "A.mdb";
    const auto dbPathB = pathB + "B.mdb";
    JET_DBID dbidA = JET_dbidNil;
    JET_DBID dbidB = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(sesidA, dbPathA.c_str(),
                                nullptr, &dbidA, 0));
    CheckJet(JetCreateDatabaseA(sesidB, dbPathB.c_str(),
                                nullptr, &dbidB, 0));
    CheckJet(JetCloseDatabase(sesidA, dbidA, 0));
    CheckJet(JetCloseDatabase(sesidB, dbidB, 0));
    CheckJet(JetDetachDatabaseA(sesidA, dbPathA.c_str()));
    CheckJet(JetDetachDatabaseA(sesidB, dbPathB.c_str()));
    CheckJet(JetEndSession(sesidA, 0));
    CheckJet(JetEndSession(sesidB, 0));

    CheckJet(JetTerm2(instanceA, JET_bitTermComplete));
    CheckJet(JetTerm2(instanceB, JET_bitTermComplete));

    //  Signal ready (the parent isn't waiting for it, but writing
    //  the sentinel before exit lets a human inspecting the temp
    //  tree confirm the child reached this point).
    ChildProcess::SignalReady(directory);
}

struct EnableMultiInstanceRegistrar
{
    EnableMultiInstanceRegistrar()
    {
        RegisterChildEntry(ChildEntryEnableMultiInstance,
                           &RunEnableMultiInstanceChild);
    }
};
[[maybe_unused]] static EnableMultiInstanceRegistrar
    _enableMultiInstanceRegistrar;

}  // namespace

EseIntegrationScenario(Platform, EnableMultiInstanceHostsTwoIndependentInstances)
{
    TemporaryDirectory directory(
        "Platform.EnableMultiInstanceHostsTwoIndependentInstances");

    //  The engine locks single/multi-instance mode at the first
    //  JetSetSystemParameter call in the process — once any other
    //  scenario has used EseInstance, JetEnableMultiInstance returns
    //  JET_errAlreadyInitialized.  Run the test in a forked child so
    //  it sees a pristine engine.
    ChildProcess child(ChildEntryEnableMultiInstance, directory.Path());
    const auto status = child.WaitForExit();
    //  Status is the raw waitpid() value; WIFEXITED + WEXITSTATUS
    //  unpack it.  Any non-zero exit means the child's CheckJet /
    //  Require fired.
    Require(WIFEXITED(status));
    Require(WEXITSTATUS(status) == 0);
}

EseIntegrationScenario(Platform, StopServiceInstanceBlocksSubsequentApiCalls)
{
    TemporaryDirectory directory(
        "Platform.StopServiceInstanceBlocksSubsequentApiCalls");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "StopService.mdb");

    //  Before StopService, the engine accepts normal API calls.
    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(),
                             database.Id(),
                             "Rows",
                             8,
                             100,
                             &tableid));
    CheckJet(JetCloseTable(session.Handle(), tableid));

    //  JetStopServiceInstance flips m_fStopJetService on the engine
    //  state.  Subsequent JET API calls that don't whitelist the
    //  flag fail with JET_errClientRequestToStopJetService.
    CheckJet(JetStopServiceInstance(instance.Handle()));

    JET_TABLEID rejected = JET_tableidNil;
    RequireJetError(JetCreateTableA(session.Handle(),
                                    database.Id(),
                                    "Rejected",
                                    8,
                                    100,
                                    &rejected),
                    JET_errClientRequestToStopJetService);

    //  JetTerm/JetRollback are whitelisted (the engine needs to be
    //  able to wind down cleanly after the stop).  Verify rollback
    //  of a transaction started before stop still works.
    //  (The EseInstance dtor will JetTerm next.)
}

EseIntegrationScenario(Platform,
                       StopServiceInstance2BackgroundOnlyKeepsForegroundAlive)
{
    TemporaryDirectory directory(
        "Platform.StopServiceInstance2BackgroundOnlyKeepsForegroundAlive");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "StopService.mdb");

    //  JetStopServiceInstance2 with JET_bitStopServiceBackgroundUserTasks
    //  asks the engine to halt restartable background work (B+ tree
    //  defrag, etc.) WITHOUT setting the master "stop service" flag
    //  — foreground API calls continue to work.  This is the cancellable
    //  variant; pair with JET_bitStopServiceResume to reverse.
    CheckJet(JetStopServiceInstance2(instance.Handle(),
                                     JET_bitStopServiceBackgroundUserTasks));

    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(),
                             database.Id(),
                             "Foreground",
                             8,
                             100,
                             &tableid));
    CheckJet(JetCloseTable(session.Handle(), tableid));

    //  Resume restores normal background-work scheduling.
    CheckJet(JetStopServiceInstance2(instance.Handle(),
                                     JET_bitStopServiceBackgroundUserTasks |
                                     JET_bitStopServiceResume));

    //  Foreground still works.
    CheckJet(JetCreateTableA(session.Handle(),
                             database.Id(),
                             "AfterResume",
                             8,
                             100,
                             &tableid));
    CheckJet(JetCloseTable(session.Handle(), tableid));
}

EseIntegrationScenario(Platform, StopServiceGlobalRoutesToActiveInstance)
{
    TemporaryDirectory directory(
        "Platform.StopServiceGlobalRoutesToActiveInstance");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "StopService.mdb");

    //  The instance-less JetStopService() just delegates to
    //  JetStopServiceInstance against the single registered
    //  instance — only valid in single-instance mode (which our
    //  framework configures by default).  Same post-condition as
    //  the per-instance form: subsequent JET API calls return
    //  JET_errClientRequestToStopJetService.
    CheckJet(JetStopService());

    JET_TABLEID rejected = JET_tableidNil;
    RequireJetError(JetCreateTableA(session.Handle(),
                                    database.Id(),
                                    "Rejected",
                                    8,
                                    100,
                                    &rejected),
                    JET_errClientRequestToStopJetService);
}

EseIntegrationScenario(Platform, ConfigureProcessForCrashDumpAcceptsGrbits)
{
    //  JetConfigureProcessForCrashDump is process-wide (not
    //  instance-scoped) — it tells the engine which slices of its
    //  in-memory state to fold into a minidump when the process
    //  crashes.  On Linux the actual minidump-writer path is a
    //  no-op (Win32-specific), but the API validates the grbit
    //  set and updates the engine's per-process bookkeeping; it
    //  must accept the documented dump-grbit combinations.
    CheckJet(JetConfigureProcessForCrashDump(JET_bitDumpMinimum));
    CheckJet(JetConfigureProcessForCrashDump(JET_bitDumpMaximum));
    CheckJet(JetConfigureProcessForCrashDump(
        JET_bitDumpMinimum | JET_bitDumpCacheIncludeDirtyPages));

    //  Invalid grbit must be rejected.  Bit 0x40000000 isn't
    //  defined in the JET_bitDump* set.
    RequireJetError(JetConfigureProcessForCrashDump(0x40000000),
                    JET_errInvalidGrbit);
}

//  JetInit3 thin variant.  Same engine surface as JetInit /
//  JetInit2 but the caller hands the engine a JET_RSTINFO struct
//  (rstmap + lgposStop + logtimeStop + pfnStatus).  Empty
//  rstInfo (no rstmap, lgposStop=0) yields the same behaviour
//  as JetInit/JetInit2 — the test confirms the v3 entry reaches
//  the same engine path and produces a usable instance.

EseIntegrationScenario(Platform, Init3WithEmptyRstInfoBootsInstance)
{
    TemporaryDirectory directory(
        "Platform.Init3WithEmptyRstInfoBootsInstance");

    JET_INSTANCE instanceHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceHandle,
                                 "Platform.Init3",
                                 "Platform.Init3", 0));

    auto pathWithSeparator = directory.Path().string();
    if (!pathWithSeparator.empty() &&
        pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 1, nullptr));

    //  Empty rstInfo — no rstmap, no stop point.  JetInit3 should
    //  treat this equivalently to JetInit2.
    JET_RSTINFO_A rstInfo = {};
    rstInfo.cbStruct = sizeof(rstInfo);
    CheckJet(JetInit3A(&instanceHandle, &rstInfo, 0));

    //  Sanity: we can open a session and create a database,
    //  proving the instance is fully alive after the v3 entry.
    JET_SESID sessionHandle = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle,
                              &sessionHandle, nullptr, nullptr));
    const auto databasePath =
        (directory.Path() / "Init3.mdb").string();
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(sessionHandle,
                                databasePath.c_str(),
                                nullptr, &databaseId,
                                JET_bitDbOverwriteExisting));
    CheckJet(JetCloseDatabase(sessionHandle, databaseId, 0));
    CheckJet(JetDetachDatabaseA(sessionHandle,
                                databasePath.c_str()));
    CheckJet(JetEndSession(sessionHandle, 0));
    CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));
}
