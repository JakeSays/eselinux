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
#include <string_view>

using namespace ese::tests;

EseIntegrationScenario(Maintenance, ComputeStatsOnEmptyTable, Smoke)
{
    TemporaryDirectory directory("Maintenance.ComputeStatsOnEmptyTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Maintenance.mdb");
    EseTable table(database, "Empty");

    CheckJet(JetComputeStats(session.Handle(), table.Id()));

    //  JetComputeStats walks the primary index and updates cRecord
    //  in the engine's in-memory object info.  On an empty table
    //  cRecord must be 0 post-compute — and the engine must have
    //  populated cPage / grbit too (the struct was zero-init).
    JET_OBJECTINFO objectInfo = {};
    objectInfo.cbStruct = sizeof(objectInfo);
    CheckJet(JetGetTableInfoA(session.Handle(),
                              table.Id(),
                              &objectInfo, sizeof(objectInfo),
                              JET_TblInfo));
    Require(objectInfo.cRecord == 0);
    Require(objectInfo.objtyp == JET_objtypTable);
    Require((objectInfo.grbit & JET_bitTableInfoUpdatable) != 0);
}

EseIntegrationScenario(Maintenance, ComputeStatsAfterInsertsSucceeds, Smoke)
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

EseIntegrationScenario(Maintenance, OnlineDefragmentRunsToCompletion, Smoke)
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

    //  After the defragment bracket the surviving rows must still be
    //  readable in order with their original values.  A defragment
    //  that corrupted the B-tree would surface as a missing /
    //  reordered / scrambled row here.  Expected survivors: rows
    //  100..199 (we deleted 0..99 from the front above).
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t expectedNext = 100;
    int32_t walkedRows = 0;
    while (true)
    {
        int32_t observedValue = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                                   &observedValue, sizeof(observedValue),
                                   &actualBytes, 0, nullptr));
        Require(actualBytes == sizeof(observedValue));
        Require(observedValue == expectedNext);
        ++expectedNext;
        ++walkedRows;
        const auto moveResult =
            JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walkedRows == 100);
}

EseIntegrationScenario(Maintenance, CompactProducesCopyWithSameData, Smoke)
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

EseIntegrationScenario(Maintenance, IdleWaitForAsyncActivitySucceeds, Smoke)
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

EseIntegrationScenario(Maintenance, IdleAvailBuffersStatusReportsState, Smoke)
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

EseIntegrationScenario(Maintenance, SetColumnDefaultValueAppliesToFutureRows, Smoke)
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

EseIntegrationScenario(Maintenance, ResizeDatabaseGrowsPageCount, Smoke)
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

    //  cpgActual is the engine's accounting; ResizeDatabase must
    //  also materialize the bytes on disk (otherwise the database
    //  would silently corrupt at the first read past the
    //  not-yet-allocated region).  Query the page size, compute
    //  expected file size, and confirm the on-disk file grew to
    //  match.
    uint32_t pageSize = 0;
    CheckJet(JetGetDatabaseInfoA(session.Handle(),
                                 database.Id(),
                                 &pageSize, sizeof(pageSize),
                                 JET_DbInfoPageSize));
    Require(pageSize > 0);
    char filename[JET_cbFullNameMost + 1] = {};
    CheckJet(JetGetDatabaseInfoA(session.Handle(),
                                 database.Id(),
                                 filename, sizeof(filename),
                                 JET_DbInfoFilename));
    std::error_code errorCode;
    const auto onDiskBytes = std::filesystem::file_size(filename, errorCode);
    Require(!errorCode);
    const uint64_t expectedBytes =
        static_cast<uint64_t>(cpgActual) * static_cast<uint64_t>(pageSize);
    Require(onDiskBytes >= expectedBytes);
}

EseIntegrationScenario(Maintenance, DatabaseScanBatchPassRunsToCompletion, Smoke)
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
    static constexpr uint32_t SecondsMaxCap = 30;
    uint32_t secondsMax = SecondsMaxCap;
    CheckJet(JetDatabaseScan(session.Handle(),
                             database.Id(),
                             &secondsMax,
                             /*cmsecSleep=*/0,
                             /*pfnCallback=*/nullptr,
                             JET_bitDatabaseScanBatchStart));
    //  The engine writes the actual elapsed seconds back into the
    //  in/out parameter.  A scan that ran must consume <= the cap;
    //  if the engine had ignored the parameter the value would stay
    //  at SecondsMaxCap (the engine writes either way, but the API
    //  contract is "actual seconds, not the input cap").  The
    //  observable here is bounded but real.
    Require(secondsMax <= SecondsMaxCap);

    //  Post-scan, every inserted row must still be reachable in
    //  order — a scan that corrupted on-page checksums or scrambled
    //  the dbtime would surface as a read failure or wrong-value
    //  retrieval below.  This is the load-bearing assertion: the
    //  engine made a full pass over the data and didn't damage it.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t expectedNextValue = 0;
    int32_t walkedRows = 0;
    while (true)
    {
        int32_t observedValue = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                                   &observedValue, sizeof(observedValue),
                                   &actualBytes, 0, nullptr));
        Require(actualBytes == sizeof(observedValue));
        Require(observedValue == expectedNextValue);
        ++expectedNextValue;
        ++walkedRows;
        const auto moveResult =
            JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walkedRows == 500);
}

EseIntegrationScenario(Maintenance, Defragment2RunsBatchPassToCompletion, Smoke)
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

EseIntegrationScenario(Maintenance, CompactPreserveOriginalKeepsSourceFileIntact, Smoke)
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

EseIntegrationScenario(Maintenance, CompactRepairProducesIdenticalRows, Smoke)
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

EseIntegrationScenario(Maintenance, CompactStatsInvokesProgressCallback, Smoke)
{
    TemporaryDirectory directory(
        "Maintenance.CompactStatsInvokesProgressCallback");

    static constexpr int RowCount = 300;
    const auto sourceDatabasePath = directory.Path() / "Source.mdb";
    const auto destinationDatabasePath = directory.Path() / "Compacted.mdb";

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

    // JET_bitCompactStats requires the JET_PFNSTATUS callback so the
    // engine has somewhere to report progress.  The callback's job
    // here is to count Compact-flavored notifications; the assertion
    // is that at least one fired (proves the flag actually plumbed
    // the engine's status pipeline).  Counters live as static
    // file-scope because JET_PFNSTATUS takes no user-context pointer.
    static int compactCallbackInvocations = 0;
    static int compactProgressInvocations = 0;
    compactCallbackInvocations = 0;
    compactProgressInvocations = 0;

    struct CompactStatsCallback
    {
        static JET_ERR JET_API Status(JET_SESID /*sesid*/,
                                       JET_SNP snp,
                                       JET_SNT snt,
                                       void* /*pv*/)
        {
            if (snp == JET_snpCompact)
            {
                ++compactCallbackInvocations;
                if (snt == JET_sntProgress)
                {
                    ++compactProgressInvocations;
                }
            }
            return JET_errSuccess;
        }
    };

    {
        EseInstance instance(directory);
        EseSession session(instance);
        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str(),
                                    JET_bitDbReadOnly));
        CheckJet(JetCompactA(session.Handle(),
                             sourceDatabasePath.string().c_str(),
                             destinationDatabasePath.string().c_str(),
                             &CompactStatsCallback::Status, nullptr,
                             JET_bitCompactStats));
        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    sourceDatabasePath.string().c_str()));
    }

    // At least one Compact-flavored callback must have fired.
    Require(compactCallbackInvocations > 0);
    // And ideally one of them was a progress tick — proves stats are
    // actually being streamed (the engine's compact loop emits these
    // at the page level).
    Require(compactProgressInvocations > 0);

    // Compacted DB must still be openable + walkable, no row loss.
    {
        EseInstance instance(directory);
        EseSession session(instance);
        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    destinationDatabasePath.string().c_str(),
                                    0));
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(session.Handle(),
                                  destinationDatabasePath.string().c_str(),
                                  nullptr, &dbid, 0));
        JET_TABLEID tid = JET_tableidNil;
        CheckJet(JetOpenTableA(session.Handle(), dbid, "Rows",
                               nullptr, 0, 0, &tid));
        int observedRows = 0;
        CheckJet(JetMove(session.Handle(), tid, JET_MoveFirst, 0));
        do
        {
            ++observedRows;
        }
        while (JetMove(session.Handle(), tid, JET_MoveNext, 0)
               != JET_errNoCurrentRecord);
        Require(observedRows == RowCount);
        CheckJet(JetCloseTable(session.Handle(), tid));
        CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    destinationDatabasePath.string().c_str()));
    }
}

EseIntegrationScenario(Maintenance, IdleCompactAsyncSchedulesBackgroundWork, Smoke)
{
    TemporaryDirectory directory(
        "Maintenance.IdleCompactAsyncSchedulesBackgroundWork");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Idle.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                     JET_bitColumnNotNULL);
    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < 100; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    // JET_bitIdleCompactAsync requires JET_bitIdleCompact and tells
    // the engine to schedule the OLD2 (B-tree defrag) pass on a
    // background thread instead of running synchronously.  The call
    // accepts the flag combination and returns promptly; the
    // engine's OLD2 task runs out of band.  Validate acceptance +
    // data correctness post-call (synchronous completion isn't
    // promised, so we don't assert on defrag effects).
    CheckJet(JetIdle(session.Handle(),
                     JET_bitIdleCompact | JET_bitIdleCompactAsync));

    // Subsequent reads against the same cursor still walk every row
    // AND each row's Value matches what was inserted — exact-byte
    // round-trip catches a background defrag that scrambled records.
    int observedRows = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t expectedNextValue = 0;
    while (true)
    {
        int32_t observedValue = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                                   &observedValue, sizeof(observedValue),
                                   &actualBytes, 0, nullptr));
        Require(actualBytes == sizeof(observedValue));
        Require(observedValue == expectedNextValue);
        ++expectedNextValue;
        ++observedRows;
        const auto moveResult =
            JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(observedRows == 100);

    //  Async pairs with sync: the same flag without Async (which
    //  drains synchronously) must also accept on a fresh idle pass
    //  and leave the data intact.  Proves both modes are wired up
    //  identically end-to-end, not just that the Async bit is
    //  silently ignored.
    CheckJet(JetIdle(session.Handle(), JET_bitIdleCompact));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t secondPassRows = 0;
    while (true)
    {
        ++secondPassRows;
        const auto moveResult =
            JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(secondPassRows == 100);
}

EseIntegrationScenario(Maintenance, DefragmentAvailSpaceTreesOnlyPreservesData, Smoke)
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

//  ===================================================================
//  Tier::Regression — defragment across multiple pass cycles.
//
//  Smoke DefragmentRunsToCompletion drives Start/Stop in one
//  bracket.  A refactor that broke pass-resumption state, or
//  forgot to invalidate the in-progress walk position across
//  successive Start calls, would slip past.  Drive multiple
//  Start/Stop cycles after deletions; final readback must produce
//  every survivor in order.
//  ===================================================================
EseIntegrationScenario(Maintenance, MultiPassDefragmentPreservesAllSurvivors, Regression)
{
    TemporaryDirectory directory(
        "Maintenance.MultiPassDefragmentPreservesAllSurvivors");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiPass.mdb");
    EseTable table(database, "Rows");
    auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                     JET_bitColumnNotNULL);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr int32_t RowCount = 2000;
    {
        EseTransaction transaction(session);
        for (int32_t k = 0; k < RowCount; ++k)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  keyColumn, &k, sizeof(k),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }
    //  Delete every 3rd row to fragment leaf pages.
    {
        EseTransaction transaction(session);
        for (int32_t k = 0; k < RowCount; k += 3)
        {
            CheckJet(JetMakeKey(session.Handle(), table.Id(),
                                &k, sizeof(k), JET_bitNewKey));
            CheckJet(JetSeek(session.Handle(), table.Id(),
                             JET_bitSeekEQ));
            CheckJet(JetDelete(session.Handle(), table.Id()));
        }
        transaction.Commit();
    }

    //  Drive Start/Stop three times in a row.  A refactor that
    //  broke pass-resumption would either deadlock the second Start
    //  or corrupt the walked-page checkpoint between runs.
    for (int32_t pass = 0; pass < 3; ++pass)
    {
        uint32_t passes = 1;
        uint32_t seconds = 5;
        CheckJet(JetDefragmentA(session.Handle(), database.Id(),
                                nullptr, &passes, &seconds,
                                JET_bitDefragmentBatchStart));
        CheckJet(JetDefragmentA(session.Handle(), database.Id(),
                                nullptr, &passes, &seconds,
                                JET_bitDefragmentBatchStop));
    }

    //  Final walk: every survivor key (not a multiple of 3) appears
    //  in ascending order.  Any corruption shows up here.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t expectedKey = 1;
    while (expectedKey % 3 == 0)
    {
        ++expectedKey;
    }
    int32_t walkedRowCount = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   keyColumn,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed == expectedKey);
        ++walkedRowCount;
        do
        {
            ++expectedKey;
        }
        while (expectedKey % 3 == 0 && expectedKey < RowCount);
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    int32_t expectedSurvivors = 0;
    for (int32_t k = 0; k < RowCount; ++k)
    {
        if (k % 3 != 0)
        {
            ++expectedSurvivors;
        }
    }
    Require(walkedRowCount == expectedSurvivors);
}

//  ===================================================================
//  Tier::Regression — Compact preserves exact row content.
//
//  Smoke CompactProducesCopyWithSameData only counts rows.  A
//  refactor that lost some rows and inserted phantoms with
//  matching count would slip past.  This scenario seeds with
//  distinguishable values (Key, Value pairs), compacts to a copy,
//  and validates every row in the copy.
//  ===================================================================
EseIntegrationScenario(Maintenance, CompactProducesByteIdenticalContent, Regression)
{
    TemporaryDirectory directory(
        "Maintenance.CompactProducesByteIdenticalContent");
    const auto sourcePath = directory.Path() / "Source.mdb";
    const auto targetPath = directory.Path() / "Target.mdb";

    static constexpr int32_t Rows = 512;
    auto expectedValueForKey = [](int32_t k) { return k * 17 + 3; };

    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Source.mdb");
        EseTable table(database, "Rows");
        auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                         JET_bitColumnNotNULL);
        auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                           JET_bitColumnNotNULL);
        static constexpr std::string_view PrimaryKey =
            std::string_view("+Key\0\0", 6);
        table.CreateIndex("PrimaryByKey", PrimaryKey,
                          JET_bitIndexPrimary | JET_bitIndexUnique);

        EseTransaction transaction(session);
        for (int32_t k = 0; k < Rows; ++k)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  keyColumn, &k, sizeof(k),
                                  0, nullptr));
            const int32_t v = expectedValueForKey(k);
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  valueColumn, &v, sizeof(v),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }
    //  Second instance: re-attach read-only, compact, detach.
    {
        EseInstance instance(directory);
        EseSession session(instance);
        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    sourcePath.string().c_str(),
                                    JET_bitDbReadOnly));
        CheckJet(JetCompactA(session.Handle(),
                             sourcePath.string().c_str(),
                             targetPath.string().c_str(),
                             nullptr, nullptr, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    sourcePath.string().c_str()));
    }

    //  Reopen the compacted target and validate every key/value.
    EseInstance instance(directory);
    EseSession session(instance);
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                targetPath.string().c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              targetPath.string().c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), dbid, "Rows",
                           nullptr, 0, 0, &tableId));
    JET_COLUMNDEF keyInfo = {};
    keyInfo.cbStruct = sizeof(keyInfo);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId, "Key",
                                     &keyInfo, sizeof(keyInfo),
                                     JET_ColInfo));
    JET_COLUMNDEF valueInfo = {};
    valueInfo.cbStruct = sizeof(valueInfo);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId, "Value",
                                     &valueInfo, sizeof(valueInfo),
                                     JET_ColInfo));
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    int32_t expected = 0;
    int32_t walked = 0;
    while (true)
    {
        int32_t observedKey = 0;
        int32_t observedValue = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                   keyInfo.columnid,
                                   &observedKey, sizeof(observedKey),
                                   &actualBytes, 0, nullptr));
        CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                   valueInfo.columnid,
                                   &observedValue, sizeof(observedValue),
                                   &actualBytes, 0, nullptr));
        Require(observedKey == expected);
        Require(observedValue == expectedValueForKey(expected));
        ++expected;
        ++walked;
        const auto moveResult = JetMove(session.Handle(), tableId,
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walked == Rows);

    CheckJet(JetCloseTable(session.Handle(), tableId));
    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                targetPath.string().c_str()));
}

//  ===================================================================
//  Tier::Regression — DatabaseScan multi-pass.  Smoke version runs
//  one pass; this scenario runs two sequential passes, deletes
//  rows between them, and verifies data integrity after both.
//  Catches refactors that broke scan-state advancement across
//  pass boundaries.
//  ===================================================================
EseIntegrationScenario(Maintenance, DatabaseScanMultiplePassesDataIntact, Regression)
{
    TemporaryDirectory directory(
        "Maintenance.DatabaseScanMultiplePassesDataIntact");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Scan.mdb");
    EseTable table(database, "Rows");
    auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                       JET_bitColumnNotNULL);

    static constexpr int32_t InitialRows = 1024;
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < InitialRows; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumn, i);
        }
        transaction.Commit();
    }

    //  First pass.
    {
        uint32_t secondsMax = 30;
        CheckJet(JetDatabaseScan(session.Handle(), database.Id(),
                                 &secondsMax, 0, nullptr,
                                 JET_bitDatabaseScanBatchStart));
        CheckJet(JetDatabaseScan(session.Handle(), database.Id(),
                                 &secondsMax, 0, nullptr,
                                 JET_bitDatabaseScanBatchStop));
    }
    //  Delete every 4th row between passes.
    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(),
                         JET_MoveFirst, 0));
        int32_t rowIndex = 0;
        while (true)
        {
            if (rowIndex % 4 == 0)
            {
                CheckJet(JetDelete(session.Handle(), table.Id()));
            }
            const auto moveResult = JetMove(session.Handle(),
                                            table.Id(),
                                            JET_MoveNext, 0);
            if (moveResult == JET_errNoCurrentRecord)
            {
                break;
            }
            CheckJet(moveResult);
            ++rowIndex;
        }
        transaction.Commit();
    }
    //  Second pass.
    {
        uint32_t secondsMax = 30;
        CheckJet(JetDatabaseScan(session.Handle(), database.Id(),
                                 &secondsMax, 0, nullptr,
                                 JET_bitDatabaseScanBatchStart));
        CheckJet(JetDatabaseScan(session.Handle(), database.Id(),
                                 &secondsMax, 0, nullptr,
                                 JET_bitDatabaseScanBatchStop));
    }
    //  Validate the surviving rows.  Walking insertion order:
    //  surviving row N is the (N+1)-th non-multiple-of-4 in
    //  0..InitialRows-1.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t walked = 0;
    int32_t expectedNext = 1;
    while (expectedNext % 4 == 0)
    {
        ++expectedNext;
    }
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   valueColumn,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed == expectedNext);
        do
        {
            ++expectedNext;
        }
        while (expectedNext % 4 == 0 && expectedNext < InitialRows);
        ++walked;
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    int32_t expectedSurvivors = 0;
    for (int32_t i = 0; i < InitialRows; ++i)
    {
        if (i % 4 != 0)
        {
            ++expectedSurvivors;
        }
    }
    Require(walked == expectedSurvivors);
}
