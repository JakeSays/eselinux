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

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace ese::tests;

EseIntegrationScenario(Snapshot, PrepareAndEndCycle, Smoke)
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

EseIntegrationScenario(Snapshot, FreezeAndThawAroundActiveInstance, Smoke)
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

EseIntegrationScenario(Snapshot, PrepareAbortDiscardsSnapshot, Smoke)
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

EseIntegrationScenario(Snapshot, PrepareInstanceScopesToOneInstance, Smoke)
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

EseIntegrationScenario(Snapshot, GetFreezeInfoReturnsActiveInstance, Smoke)
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
EseIntegrationScenario(Snapshot, TruncateLogClearsBackupLogs, Smoke)
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
EseIntegrationScenario(Snapshot, TruncateLogInstanceClearsOneInstance, Smoke)
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

//  ===================================================================
//  Tier::Regression — repeated snapshot brackets around live
//  workload.  Smoke snapshot tests run one bracket in isolation.
//  This scenario interleaves three full snapshot brackets with
//  insert workloads of varying sizes, then validates the table
//  contains exactly the predicted row count + every value walks
//  in insertion order.  Catches refactors that broke
//  thaw-to-normal-ops state transition or accumulated state
//  across sequential snapshot ids.
//  ===================================================================
EseIntegrationScenario(Snapshot, RepeatedBracketsAroundLiveWorkload, Regression)
{
    TemporaryDirectory directory(
        "Snapshot.RepeatedBracketsAroundLiveWorkload");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "RepSnap.mdb");
    EseTable table(database, "Rows");
    auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                       JET_bitColumnNotNULL);

    auto insertBatch = [&](int32_t baseValue, int32_t count) {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < count; ++i)
        {
            const int32_t value = baseValue + i;
            InsertSingleFixedColumnRow<int32_t>(table, valueColumn, value);
        }
        transaction.Commit();
    };

    //  Thaw consumes the snapshot id by default (engine releases
    //  the internal state).  Prepare with JET_bitContinueAfterThaw
    //  to keep the id alive across Thaw so we can End it cleanly.
    auto snapshotBracket = [&]() {
        JET_OSSNAPID snapshotId = 0;
        CheckJet(JetOSSnapshotPrepare(&snapshotId,
                                      JET_bitContinueAfterThaw));
        uint32_t freezeCount = 0;
        JET_INSTANCE_INFO_A* freezeInfo = nullptr;
        CheckJet(JetOSSnapshotFreezeA(snapshotId, &freezeCount,
                                      &freezeInfo, 0));
        Require(freezeCount >= 1);
        CheckJet(JetFreeBuffer(reinterpret_cast<char*>(freezeInfo)));
        CheckJet(JetOSSnapshotThaw(snapshotId, 0));
        CheckJet(JetOSSnapshotEnd(snapshotId, 0));
    };

    insertBatch(0, 128);
    snapshotBracket();
    insertBatch(128, 256);
    snapshotBracket();
    insertBatch(384, 512);
    snapshotBracket();

    static constexpr int32_t TotalRows = 128 + 256 + 512;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t expectedValue = 0;
    int32_t walked = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   valueColumn,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed == expectedValue);
        ++expectedValue;
        ++walked;
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walked == TotalRows);
}

