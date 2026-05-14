// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/CrashHelper.hxx"
#include "Framework/EseInstance.hxx"
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
