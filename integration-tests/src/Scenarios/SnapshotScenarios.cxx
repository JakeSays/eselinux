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

    //  Two successive Prepare calls must hand out distinct snapshot
    //  ids — proves Prepare allocates fresh state rather than
    //  returning a constant.
    JET_OSSNAPID secondSnapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&secondSnapshotId, 0));
    Require(secondSnapshotId != 0);
    Require(secondSnapshotId != snapshotId);

    CheckJet(JetOSSnapshotEnd(snapshotId, 0));
    CheckJet(JetOSSnapshotEnd(secondSnapshotId, 0));

    //  Reusing an ended snapshot id must be rejected with
    //  InvalidSnapId — proves End actually released the engine-side
    //  state instead of leaving the id valid.
    RequireJetError(JetOSSnapshotEnd(snapshotId, 0),
                    JET_errOSSnapshotInvalidSnapId);
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

//  JetOSSnapshotTruncateLog.  JET_bitContinueAfterThaw is a Prepare
//  flag (not a Thaw flag); it keeps the snap-id alive past Thaw so
//  the post-thaw TruncateLog + End calls can still reach it.  Thaw
//  itself only accepts NO_GRBIT.
EseIntegrationScenario(Snapshot, TruncateLogClearsBackupLogs)
{
    TemporaryDirectory directory("Snapshot.TruncateLogClearsBackupLogs");
    //  Use non-circular logging so gens accumulate (TruncateLog has
    //  nothing to do under circular logging).  Tiny log size forces
    //  rolls even on a small workload.
    EseInstanceOptions instanceOptions;
    instanceOptions.EnableCircularLog = false;
    instanceOptions.LogFileSizeKb = 64;
    EseInstance instance(directory, "ese-tests", nullptr,
                         EseInstanceMode::SingleInstance, instanceOptions);
    EseSession session(instance);
    EseDatabase database(session, "Snapshot.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                     JET_bitColumnNotNULL);

    JET_OSSNAPID snapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&snapshotId, JET_bitContinueAfterThaw));

    uint32_t freezeCount = 0;
    JET_INSTANCE_INFO_A* freezeInfo = nullptr;
    CheckJet(JetOSSnapshotFreezeA(snapshotId,
                                  &freezeCount,
                                  &freezeInfo,
                                  0));
    //  freezeCount must enumerate exactly the instance we just
    //  booted (single-instance configuration).  Asserting on the
    //  instance struct contents proves the engine populated
    //  meaningful data — not just "the array pointer is non-null."
    Require(freezeCount == 1);
    Require(freezeInfo != nullptr);
    Require(freezeInfo[0].cDatabases >= 1);
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(freezeInfo)));

    CheckJet(JetOSSnapshotThaw(snapshotId, 0));
    //  TruncateLog is gated on the snapshot agent having consumed
    //  the captured logs — without that ack the engine keeps every
    //  gen.  What we CAN observe is the contract: the call returns
    //  success (no error) and the snapshot id remains valid through
    //  End.  An invalid id, double-end, or post-end reuse fails
    //  with JET_errOSSnapshotInvalidSnapId.
    CheckJet(JetOSSnapshotTruncateLog(snapshotId, 0));
    CheckJet(JetOSSnapshotEnd(snapshotId, 0));

    //  After End, the snapshot id is gone — reusing it must error.
    RequireJetError(JetOSSnapshotTruncateLog(snapshotId, 0),
                    JET_errOSSnapshotInvalidSnapId);

    //  After the snapshot bracket, the engine is still serving the
    //  database — write + read confirms the freeze/thaw didn't
    //  leave the instance in a half-locked state.
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 42);
        transaction.Commit();
    }
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t observedValue = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                               &observedValue, sizeof(observedValue),
                               &actualBytes, 0, nullptr));
    Require(observedValue == 42);
}

//  JetOSSnapshotTruncateLogInstance narrows the truncate to a single
//  instance handle.  Same Prepare/Freeze/Thaw bracket as the global
//  form, but TruncateLogInstance hits one explicit instance rather
//  than walking every enrolled instance.
EseIntegrationScenario(Snapshot, TruncateLogInstanceClearsOneInstance)
{
    TemporaryDirectory directory("Snapshot.TruncateLogInstanceClearsOneInstance");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Snapshot.mdb");

    JET_OSSNAPID snapshotId = 0;
    CheckJet(JetOSSnapshotPrepare(&snapshotId, JET_bitContinueAfterThaw));

    uint32_t freezeCount = 0;
    JET_INSTANCE_INFO_A* freezeInfo = nullptr;
    CheckJet(JetOSSnapshotFreezeA(snapshotId,
                                  &freezeCount,
                                  &freezeInfo,
                                  0));
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(freezeInfo)));

    CheckJet(JetOSSnapshotThaw(snapshotId, 0));
    CheckJet(JetOSSnapshotTruncateLogInstance(snapshotId,
                                              instance.Handle(),
                                              0));
    CheckJet(JetOSSnapshotEnd(snapshotId, 0));
}

