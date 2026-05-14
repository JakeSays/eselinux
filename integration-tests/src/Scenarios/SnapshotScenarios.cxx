// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/RowOperations.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <jetapi.h>

using namespace ese::tests;

EseIntegrationScenario(Snapshot, PrepareAndEndCycle)
{
    TemporaryDirectory directory("Snapshot.PrepareAndEndCycle");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Snapshot.mdb");

    JET_OSSNAPID snapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&snapshotId, 0));
    Require(snapshotId != 0);
    CheckJet(JetOSSnapshotEnd(snapshotId, 0));
}

EseIntegrationScenario(Snapshot, FreezeAndThawAroundActiveInstance)
{
    TemporaryDirectory directory("Snapshot.FreezeAndThawAroundActiveInstance");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Snapshot.mdb");

    JET_OSSNAPID snapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&snapshotId, 0));

    uint32_t instanceInfoCount = 0;
    JET_INSTANCE_INFO_A* instanceInfoArray = nullptr;
    CheckJet(JetOSSnapshotFreezeA(snapshotId,
                                  &instanceInfoCount,
                                  &instanceInfoArray,
                                  0));
    Require(instanceInfoCount >= 1);

    // Engine allocated instanceInfoArray via JetMalloc; release it.
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(instanceInfoArray)));

    // Thaw concludes the snapshot lifecycle; calling JetOSSnapshotEnd
    // after Thaw fails with JET_errOSSnapshotInvalidSnapId — the
    // engine has already released the id.
    CheckJet(JetOSSnapshotThaw(snapshotId, 0));
}

EseIntegrationScenario(Snapshot, PrepareAbortDiscardsSnapshot)
{
    TemporaryDirectory directory("Snapshot.PrepareAbortDiscardsSnapshot");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Snapshot.mdb");

    //  JetOSSnapshotAbort is the early-exit path: after Prepare,
    //  before Freeze, the caller can decide not to proceed.  Abort
    //  releases the snapshot id; subsequent use returns
    //  JET_errOSSnapshotInvalidSnapId.
    JET_OSSNAPID snapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&snapshotId, 0));
    CheckJet(JetOSSnapshotAbort(snapshotId, 0));
}

EseIntegrationScenario(Snapshot, PrepareInstanceScopesToOneInstance)
{
    TemporaryDirectory directory("Snapshot.PrepareInstanceScopesToOneInstance");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Snapshot.mdb");

    //  Two-stage prepare: JetOSSnapshotPrepare opens a snapshot id.
    //  Default mode auto-enrols every running instance at freeze
    //  time; JET_bitExplicitPrepare disables that auto-enrol so the
    //  caller can opt in per-instance via JetOSSnapshotPrepareInstance
    //  — required when the host process wants to snapshot only some
    //  of the engines it owns.
    JET_OSSNAPID snapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&snapshotId,
                                  JET_bitExplicitPrepare));
    CheckJet(JetOSSnapshotPrepareInstance(snapshotId,
                                          instance.Handle(),
                                          0));

    uint32_t freezeCount = 0;
    JET_INSTANCE_INFO_A* freezeInfo = nullptr;
    CheckJet(JetOSSnapshotFreezeA(snapshotId,
                                  &freezeCount,
                                  &freezeInfo,
                                  0));
    Require(freezeCount == 1);
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(freezeInfo)));

    //  Default (no JET_bitContinueAfterThaw) — Thaw retires the snap
    //  id; no End call needed.
    CheckJet(JetOSSnapshotThaw(snapshotId, 0));
}

EseIntegrationScenario(Snapshot, GetFreezeInfoReturnsActiveInstance)
{
    TemporaryDirectory directory("Snapshot.GetFreezeInfoReturnsActiveInstance");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Snapshot.mdb");

    JET_OSSNAPID snapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&snapshotId, 0));

    uint32_t freezeCount = 0;
    JET_INSTANCE_INFO_A* freezeInfo = nullptr;
    CheckJet(JetOSSnapshotFreezeA(snapshotId,
                                  &freezeCount,
                                  &freezeInfo,
                                  0));
    Require(freezeCount >= 1);
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(freezeInfo)));

    //  JetOSSnapshotGetFreezeInfo re-fetches the same data the
    //  Freeze call returned, without freezing again — useful for
    //  backup agents that want to refresh their view mid-snapshot.
    uint32_t infoCount = 0;
    JET_INSTANCE_INFO_A* infoArray = nullptr;
    CheckJet(JetOSSnapshotGetFreezeInfoA(snapshotId,
                                         &infoCount,
                                         &infoArray,
                                         0));
    Require(infoCount >= 1);
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(infoArray)));

    CheckJet(JetOSSnapshotThaw(snapshotId, 0));
}

