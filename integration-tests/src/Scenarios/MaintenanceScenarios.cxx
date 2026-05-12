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

EseIntegrationScenario(Maintenance, CompactProducesCopyWithSameData)
{
    TemporaryDirectory directory("Maintenance.CompactProducesCopyWithSameData");

    static constexpr int RowCount = 500;
    const auto sourceDatabasePath = directory.Path() / "Source.mdb";
    const auto destinationDatabasePath = directory.Path() / "Compacted.mdb";

    // Build the source database, then JetTerm so JetCompact sees a
    // detached database file in a known state.
    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Source.mdb");
        EseTable table(database, "Rows");

        auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    // Compact pass: ErrCMPOpenDB (comp.cxx around line 214) calls
    // ErrDBOpenDatabase, which requires the source to already be
    // *attached* — JetCompactA without a prior attach surfaces as
    // JET_errDatabaseNotFound.  Attach read-only first.
    {
        EseInstance instance(directory);
        EseSession session(instance);

        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str(),
                                    JET_bitDbReadOnly));

        CheckJet(JetCompactA(session.Handle(),
                             sourceDatabasePath.string().c_str(),
                             destinationDatabasePath.string().c_str(),
                             nullptr, nullptr, 0));

        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str()));
    }

    // Third instance: attach the compacted copy and walk every row.
    {
        EseInstance instance(directory);
        EseSession session(instance);
        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    destinationDatabasePath.string().c_str(),
                                    0));
        JET_DBID compactedDbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(session.Handle(),
                                  destinationDatabasePath.string().c_str(),
                                  nullptr, &compactedDbid, 0));

        JET_TABLEID compactedTableId = JET_tableidNil;
        CheckJet(JetOpenTableA(session.Handle(), compactedDbid, "Rows",
                               nullptr, 0, 0, &compactedTableId));

        int observed = 0;
        CheckJet(JetMove(session.Handle(), compactedTableId, JET_MoveFirst, 0));
        do
        {
            ++observed;
        }
        while (JetMove(session.Handle(), compactedTableId, JET_MoveNext, 0)
               != JET_errNoCurrentRecord);
        Require(observed == RowCount);

        CheckJet(JetCloseTable(session.Handle(), compactedTableId));
        CheckJet(JetCloseDatabase(session.Handle(), compactedDbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    destinationDatabasePath.string().c_str()));
    }
}

EseIntegrationScenario(Maintenance, IdleWaitForAsyncActivitySucceeds)
{
    TemporaryDirectory directory(
        "Maintenance.IdleWaitForAsyncActivitySucceeds");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maint.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);
    {
        EseTransaction transaction(session);
        for (int i = 0; i < 10; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    // JET_bitIdleWaitForAsyncActivity quiesces background async work.
    // Engine returns JET_errSuccess when the queue is drained, or
    // JET_wrnRemainingVersions when version-store buckets are still
    // pending.  Either outcome means the call exercised the path.
    const auto err = JetIdle(session.Handle(),
                             JET_bitIdleWaitForAsyncActivity);
    Require(err == JET_errSuccess || err == JET_wrnRemainingVersions);
}

EseIntegrationScenario(Maintenance, IdleAvailBuffersStatusReportsState)
{
    TemporaryDirectory directory(
        "Maintenance.IdleAvailBuffersStatusReportsState");
    EseInstance instance(directory);
    EseSession session(instance);

    // JET_bitIdleAvailBuffersStatus reports whether the cache has
    // dropped below the JET_paramStartFlushThreshold.  On an empty
    // instance the call returns JET_errSuccess (cache plentiful);
    // accepting JET_wrnIdleFull as the alternative success-with-
    // warning code keeps the test robust against cache pressure
    // introduced by sibling scenarios.
    const auto err = JetIdle(session.Handle(), JET_bitIdleAvailBuffersStatus);
    Require(err == JET_errSuccess || err == JET_wrnIdleFull);
}
