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

EseIntegrationScenario(Maintenance, SetColumnDefaultValueAppliesToFutureRows)
{
    TemporaryDirectory directory(
        "Maintenance.SetColumnDefaultValueAppliesToFutureRows");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");
    EseTable table(database, "Defaults");

    auto columnId = table.AddColumn("Value", JET_coltypLong);

    //  Set a column-level default; rows inserted without a value for
    //  Value see the engine fill in 42.
    int32_t defaultValue = 42;
    CheckJet(JetSetColumnDefaultValueA(session.Handle(),
                                       database.Id(),
                                       "Defaults",
                                       "Value",
                                       &defaultValue,
                                       sizeof(defaultValue),
                                       0));

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  table.Id(),
                                  JET_prepInsert));
        CheckJet(JetUpdate(session.Handle(),
                           table.Id(),
                           nullptr,
                           0,
                           nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    const auto observed =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId);
    Require(observed == defaultValue);
}

EseIntegrationScenario(Maintenance, ResizeDatabaseGrowsPageCount)
{
    TemporaryDirectory directory(
        "Maintenance.ResizeDatabaseGrowsPageCount");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");

    //  Find the current page count, ask the engine to grow to that +
    //  256 pages.  JetResizeDatabase honours JET_bitResizeDatabaseOnlyGrow
    //  so we can be certain we never shrink mid-test.
    uint32_t cpgBefore = 0;
    CheckJet(JetGetDatabaseInfoA(session.Handle(),
                                 database.Id(),
                                 &cpgBefore,
                                 sizeof(cpgBefore),
                                 JET_DbInfoFilesize));

    const uint32_t cpgTarget = cpgBefore + 256;
    uint32_t cpgActual = 0;
    CheckJet(JetResizeDatabase(session.Handle(),
                               database.Id(),
                               cpgTarget,
                               &cpgActual,
                               JET_bitResizeDatabaseOnlyGrow));
    Require(cpgActual >= cpgTarget);
}

EseIntegrationScenario(Maintenance, DatabaseScanBatchPassRunsToCompletion)
{
    TemporaryDirectory directory(
        "Maintenance.DatabaseScanBatchPassRunsToCompletion");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Scan.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);

    //  Populate enough rows that the scan touches at least a few
    //  pages.  Database Maintenance walks every leaf page checking
    //  on-page checksums and refreshing the dbtime baseline.
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 500; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  JET_bitDatabaseScanBatchStart asks the engine to perform one
    //  synchronous scan pass; pcSecondsMax bounds the run.  On a tiny
    //  database the call completes well inside the cap; the engine
    //  updates pcSecondsMax in place with the seconds actually used.
    uint32_t secondsMax = 30;
    CheckJet(JetDatabaseScan(session.Handle(),
                             database.Id(),
                             &secondsMax,
                             /*cmsecSleep=*/0,
                             /*pfnCallback=*/nullptr,
                             JET_bitDatabaseScanBatchStart));
}

EseIntegrationScenario(Maintenance, Defragment2RunsBatchPassToCompletion)
{
    TemporaryDirectory directory(
        "Maintenance.Defragment2RunsBatchPassToCompletion");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);

    //  Generate enough activity that the defrag has something to do.
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 500; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  JetDefragment2 adds a JET_CALLBACK progress hook to the v1
    //  shape.  Start with one batch pass capped at 30s; the engine
    //  reports the actual seconds + passes consumed.  On a tiny
    //  database both come back near zero — what we care about is
    //  that the v2 entry point reaches the same engine path.
    uint32_t passes = 1;
    uint32_t seconds = 30;
    CheckJet(JetDefragment2A(session.Handle(),
                             database.Id(),
                             /*szTableName=*/nullptr,
                             &passes,
                             &seconds,
                             /*callback=*/nullptr,
                             JET_bitDefragmentBatchStart));

    //  Stop the background defrag so the next scenario doesn't
    //  inherit a running thread.
    passes = 1;
    seconds = 30;
    CheckJet(JetDefragment2A(session.Handle(),
                             database.Id(),
                             nullptr,
                             &passes,
                             &seconds,
                             nullptr,
                             JET_bitDefragmentBatchStop));
}

//  JetDefragment3 is an UPSTREAM ENGINE STUB —
//  `dev/ese/src/ese/jetapi.cxx:20064` returns
//  `JET_errInvalidParameter` unconditionally with a "OBSOLETE:
//  only used by SFS" comment.  Coverage lists it under the
//  out-of-scope stubs, not as a gap.

EseIntegrationScenario(Maintenance, CompactPreserveOriginalKeepsSourceFileIntact)
{
    TemporaryDirectory directory(
        "Maintenance.CompactPreserveOriginalKeepsSourceFileIntact");

    static constexpr int RowCount = 200;
    const auto sourceDatabasePath = directory.Path() / "Source.mdb";
    const auto destinationDatabasePath = directory.Path() / "Compacted.mdb";

    // Seed the source database, then JetTerm so JetCompact sees a
    // detached file in a known state.
    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Source.mdb");
        EseTable table(database, "Rows");
        auto columnId = table.AddColumn("Value", JET_coltypLong,
                                         JET_bitColumnNotNULL);
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    // Snapshot the on-disk size of the source so we can reconcile
    // afterward.  A compact run without PreserveOriginal would
    // typically replace the file in-place; with the flag the source
    // must remain byte-equivalent.
    const auto sizeBefore = std::filesystem::file_size(sourceDatabasePath);

    {
        EseInstance instance(directory);
        EseSession session(instance);

        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str(),
                                    JET_bitDbReadOnly));

        CheckJet(JetCompactA(session.Handle(),
                             sourceDatabasePath.string().c_str(),
                             destinationDatabasePath.string().c_str(),
                             nullptr, nullptr,
                             JET_bitCompactPreserveOriginal));

        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str()));
    }

    // Source must still exist with the same size.
    Require(std::filesystem::exists(sourceDatabasePath));
    Require(std::filesystem::file_size(sourceDatabasePath) == sizeBefore);

    // And the compacted destination is a real, openable database.
    Require(std::filesystem::exists(destinationDatabasePath));

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
        JET_TABLEID tableId = JET_tableidNil;
        CheckJet(JetOpenTableA(session.Handle(), compactedDbid, "Rows",
                               nullptr, 0, 0, &tableId));

        int observedCount = 0;
        CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
        do
        {
            ++observedCount;
        }
        while (JetMove(session.Handle(), tableId, JET_MoveNext, 0)
               != JET_errNoCurrentRecord);
        Require(observedCount == RowCount);

        CheckJet(JetCloseTable(session.Handle(), tableId));
        CheckJet(JetCloseDatabase(session.Handle(), compactedDbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    destinationDatabasePath.string().c_str()));
    }
}

EseIntegrationScenario(Maintenance, CompactRepairProducesIdenticalRows)
{
    TemporaryDirectory directory(
        "Maintenance.CompactRepairProducesIdenticalRows");

    static constexpr int RowCount = 250;
    const auto sourceDatabasePath = directory.Path() / "Source.mdb";
    const auto destinationDatabasePath = directory.Path() / "Repaired.mdb";

    // Seed the source database with a known monotonic sequence so
    // the compacted copy is verifiable row-by-row.
    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Source.mdb");
        EseTable table(database, "Rows");
        auto columnId = table.AddColumn("Value", JET_coltypLong,
                                         JET_bitColumnNotNULL);

        static constexpr std::string_view PrimaryKeyDescriptor =
            std::string_view("+Value\0\0", 8);
        table.CreateIndex("ByValue", PrimaryKeyDescriptor,
                          JET_bitIndexPrimary | JET_bitIndexUnique);

        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    // Compact in repair mode: JET_bitCompactRepair tells the engine
    // to skip preread and tolerate duplicate keys.  On a healthy
    // source the data must come through unchanged.
    {
        EseInstance instance(directory);
        EseSession session(instance);

        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str(),
                                    JET_bitDbReadOnly));

        CheckJet(JetCompactA(session.Handle(),
                             sourceDatabasePath.string().c_str(),
                             destinationDatabasePath.string().c_str(),
                             nullptr, nullptr,
                             JET_bitCompactRepair));

        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str()));
    }

    // Walk the repaired copy and confirm every Value 0..RowCount-1
    // is present exactly once, in order.
    {
        EseInstance instance(directory);
        EseSession session(instance);
        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    destinationDatabasePath.string().c_str(),
                                    0));
        JET_DBID repairedDbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(session.Handle(),
                                  destinationDatabasePath.string().c_str(),
                                  nullptr, &repairedDbid, 0));
        JET_TABLEID tableId = JET_tableidNil;
        CheckJet(JetOpenTableA(session.Handle(), repairedDbid, "Rows",
                               nullptr, 0, 0, &tableId));

        JET_COLUMNDEF columnDef = {};
        columnDef.cbStruct = sizeof(columnDef);
        CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId,
                                        "Value", &columnDef,
                                        sizeof(columnDef), JET_ColInfo));

        int expectedValue = 0;
        CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
        do
        {
            int32_t value = 0;
            uint32_t actualBytes = 0;
            CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                       columnDef.columnid,
                                       &value, sizeof(value),
                                       &actualBytes, 0, nullptr));
            Require(value == expectedValue);
            ++expectedValue;
        }
        while (JetMove(session.Handle(), tableId, JET_MoveNext, 0)
               != JET_errNoCurrentRecord);
        Require(expectedValue == RowCount);

        CheckJet(JetCloseTable(session.Handle(), tableId));
        CheckJet(JetCloseDatabase(session.Handle(), repairedDbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    destinationDatabasePath.string().c_str()));
    }
}

EseIntegrationScenario(Maintenance, DefragmentAvailSpaceTreesOnlyPreservesData)
{
    TemporaryDirectory directory(
        "Maintenance.DefragmentAvailSpaceTreesOnlyPreservesData");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                     JET_bitColumnNotNULL);

    static constexpr int RowCount = 400;
    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    // Delete every other row to create freelist churn — the engine
    // has actual work to do on the AvailExt tree.
    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        int rowIndex = 0;
        while (true)
        {
            if ((rowIndex & 1) != 0)
            {
                CheckJet(JetDelete(session.Handle(), table.Id()));
            }
            ++rowIndex;
            const JET_ERR moveResult =
                JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
            if (moveResult == JET_errNoCurrentRecord)
            {
                break;
            }
            CheckJet(moveResult);
        }
        transaction.Commit();
    }

    // Run defrag in AvailSpaceTreesOnly mode — the engine restricts
    // the pass to the AvailExt B-trees (no data-tree work).  Combine
    // with BatchStart so the engine accepts the pass parameters.
    uint32_t passes = 1;
    uint32_t seconds = 30;
    CheckJet(JetDefragment2A(session.Handle(), database.Id(),
                             nullptr,
                             &passes, &seconds, nullptr,
                             JET_bitDefragmentBatchStart
                             | JET_bitDefragmentAvailSpaceTreesOnly));

    // Stop the background defrag so the next scenario doesn't inherit
    // a running pass.
    passes = 1;
    seconds = 30;
    CheckJet(JetDefragment2A(session.Handle(), database.Id(),
                             nullptr,
                             &passes, &seconds, nullptr,
                             JET_bitDefragmentBatchStop));

    // Data still walks correctly after the AvailSpace pass —
    // RowCount/2 even rows survive (0, 2, 4, ...).
    int observedCount = 0;
    int lastObservedValue = -1;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        int32_t value = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                                   &value, sizeof(value),
                                   &actualBytes, 0, nullptr));
        Require(value == lastObservedValue + 2 || lastObservedValue == -1);
        Require((value & 1) == 0);
        lastObservedValue = value;
        ++observedCount;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observedCount == RowCount / 2);
}
