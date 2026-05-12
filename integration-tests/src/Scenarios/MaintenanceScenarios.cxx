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

#include <filesystem>
#include <string>

using namespace ese::tests;

EseIntegrationScenario(Maintenance, ComputeStatsOnEmptyTable)
{
    TemporaryDirectory directory("Maintenance.ComputeStatsOnEmptyTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");
    EseTable table(database, "Empty");

    CheckJet(JetComputeStats(session.Handle(), table.Id()));
}

EseIntegrationScenario(Maintenance, ComputeStatsAfterInsertsSucceeds)
{
    TemporaryDirectory directory("Maintenance.ComputeStatsAfterInsertsSucceeds");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");
    EseTable table(database, "Populated");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < 100; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    CheckJet(JetComputeStats(session.Handle(), table.Id()));
}

EseIntegrationScenario(Maintenance, OnlineDefragmentRunsToCompletion)
{
    TemporaryDirectory directory("Maintenance.OnlineDefragmentRunsToCompletion");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");
    EseTable table(database, "Frag");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    // Populate, delete, repopulate to give defragment something to do.
    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < 200; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }
    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        for (int rowIndex = 0; rowIndex < 100; ++rowIndex)
        {
            CheckJet(JetDelete(session.Handle(), table.Id()));
            const auto moveResult =
                JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
            if (moveResult == JET_errNoCurrentRecord)
            {
                break;
            }
            CheckJet(moveResult);
        }
        transaction.Commit();
    }

    // Defragment in batched mode — Start kicks off, Stop drains.
    uint32_t passes = 1;
    uint32_t seconds = 1;
    CheckJet(JetDefragmentA(session.Handle(),
                            database.Id(),
                            nullptr,
                            &passes,
                            &seconds,
                            JET_bitDefragmentBatchStart));
    CheckJet(JetDefragmentA(session.Handle(),
                            database.Id(),
                            nullptr,
                            &passes,
                            &seconds,
                            JET_bitDefragmentBatchStop));
}

// TODO Phase 5: JetCompactA round-trip.  Detaching the source via
// EseDatabase's dtor + re-attaching in a fresh session reports
// JET_errDatabaseNotFound from JetCompactA, suggesting the engine
// still considers the source "claimed" until JetTerm.  Worth
// investigating with a per-scenario JetTerm cycle (likely via
// CrashHelper) when we revisit Compact in Phase 5.
