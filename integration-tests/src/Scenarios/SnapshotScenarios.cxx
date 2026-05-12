// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
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
