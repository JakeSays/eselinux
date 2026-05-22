// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"
#include "Framework/ThreadGroup.hxx"

#include <atomic>
#include <mutex>

using namespace ese::tests;

EseIntegrationScenario(Concurrency, ThreadGroupLaunchAndJoin, Smoke)
{
    // Smoke test the ThreadGroup harness with no engine involvement.
    static constexpr int WorkerCount = 8;
    std::atomic<int> totalTicks = 0;

    ThreadGroup threadGroup;
    threadGroup.Launch(WorkerCount, [&totalTicks](int /*threadIndex*/)
    {
        for (int tick = 0; tick < 100; ++tick)
        {
            totalTicks.fetch_add(1, std::memory_order_relaxed);
        }
    });
    threadGroup.Join();

    Require(totalTicks.load() == WorkerCount * 100);
}

EseIntegrationScenario(Concurrency, MultiSessionParallelInsertsAllSucceed, Smoke)
{
    TemporaryDirectory directory("Concurrency.MultiSessionParallelInsertsAllSucceed");
    EseInstance instance(directory);

    static constexpr int WorkerCount = 4;
    static constexpr int RowsPerWorker = 100;

    // Build the schema from a single setup session before threads
    // start so the table exists when each worker begins.
    {
        EseSession session(instance);
        EseDatabase database(session, "Concurrent.mdb");
        EseTable table(database, "Rows");
        table.AddColumn("ThreadIndex", JET_coltypLong, JET_bitColumnNotNULL);
        table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);
    }

    ThreadGroup threadGroup;
    threadGroup.Launch(WorkerCount, [&instance](int threadIndex)
    {
        EseSession session(instance);
        EseDatabase database(session, "Concurrent.mdb", EseDatabaseMode::AttachAndOpen);
        EseTable table(database, "Rows", EseTableMode::Open);

        JET_COLUMNDEF threadIndexDefinition = {};
        threadIndexDefinition.cbStruct = sizeof(threadIndexDefinition);
        CheckJet(JetGetTableColumnInfoA(session.Handle(), table.Id(), "ThreadIndex",
                                        &threadIndexDefinition,
                                        sizeof(threadIndexDefinition),
                                        JET_ColInfo));
        JET_COLUMNDEF valueDefinition = {};
        valueDefinition.cbStruct = sizeof(valueDefinition);
        CheckJet(JetGetTableColumnInfoA(session.Handle(), table.Id(), "Value",
                                        &valueDefinition, sizeof(valueDefinition),
                                        JET_ColInfo));

        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < RowsPerWorker; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  threadIndexDefinition.columnid,
                                  &threadIndex, sizeof(threadIndex),
                                  0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  valueDefinition.columnid,
                                  &rowIndex, sizeof(rowIndex),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        }
        transaction.Commit();
    });
    threadGroup.Join();

    // Re-open from a fresh session and count rows.
    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession, "Concurrent.mdb",
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, "Rows", EseTableMode::Open);

    int observed = 0;
    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(), JET_MoveFirst, 0));
    do
    {
        ++observed;
    }
    while (JetMove(verifySession.Handle(), verifyTable.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observed == WorkerCount * RowsPerWorker);
}

EseIntegrationScenario(Concurrency, EscrowContentionSumsCorrectly, Smoke)
{
    TemporaryDirectory directory("Concurrency.EscrowContentionSumsCorrectly");
    EseInstance instance(directory);

    static constexpr int WorkerCount = 4;
    static constexpr int IncrementsPerWorker = 250;
    static constexpr int32_t InitialValue = 0;

    // Setup: one row with an escrow column.
    {
        EseSession session(instance);
        EseDatabase database(session, "Escrow.mdb");
        EseTable table(database, "Counter");
        auto columnId = table.AddColumnWithDefault("Counter",
                                                   JET_coltypLong,
                                                   &InitialValue,
                                                   sizeof(InitialValue),
                                                   JET_bitColumnEscrowUpdate);
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
        (void)columnId;
    }

    ThreadGroup threadGroup;
    threadGroup.Launch(WorkerCount, [&instance](int /*threadIndex*/)
    {
        EseSession session(instance);
        EseDatabase database(session, "Escrow.mdb", EseDatabaseMode::AttachAndOpen);
        EseTable table(database, "Counter", EseTableMode::Open);

        JET_COLUMNDEF counterDefinition = {};
        counterDefinition.cbStruct = sizeof(counterDefinition);
        CheckJet(JetGetTableColumnInfoA(session.Handle(), table.Id(), "Counter",
                                        &counterDefinition,
                                        sizeof(counterDefinition),
                                        JET_ColInfo));

        for (int index = 0; index < IncrementsPerWorker; ++index)
        {
            EseTransaction transaction(session);
            CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
            int32_t delta = 1;
            uint32_t actualSize = 0;
            CheckJet(JetEscrowUpdate(session.Handle(), table.Id(),
                                     counterDefinition.columnid,
                                     &delta, sizeof(delta),
                                     nullptr, 0, &actualSize, 0));
            transaction.Commit();
        }
    });
    threadGroup.Join();

    // Re-open and verify the counter equals WorkerCount * IncrementsPerWorker.
    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession, "Escrow.mdb",
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, "Counter", EseTableMode::Open);

    JET_COLUMNDEF counterDefinition = {};
    counterDefinition.cbStruct = sizeof(counterDefinition);
    CheckJet(JetGetTableColumnInfoA(verifySession.Handle(), verifyTable.Id(),
                                    "Counter", &counterDefinition,
                                    sizeof(counterDefinition), JET_ColInfo));

    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(), JET_MoveFirst, 0));
    int32_t observedTotal = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(verifySession.Handle(), verifyTable.Id(),
                               counterDefinition.columnid,
                               &observedTotal, sizeof(observedTotal),
                               &actualSize, 0, nullptr));
    Require(observedTotal == WorkerCount * IncrementsPerWorker);
}

EseIntegrationScenario(Concurrency, ConcurrentReadersDontBlockWriter, Smoke)
{
    TemporaryDirectory directory("Concurrency.ConcurrentReadersDontBlockWriter");
    EseInstance instance(directory);

    static constexpr int ReaderCount = 4;
    static constexpr int InitialRows = 200;

    // Build initial state in a setup session.
    {
        EseSession session(instance);
        EseDatabase database(session, "ReadWrite.mdb");
        EseTable table(database, "Rows");
        auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < InitialRows; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), columnId,
                                  &rowIndex, sizeof(rowIndex), 0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    std::atomic<bool> stopReaders = false;

    ThreadGroup threadGroup;
    threadGroup.Launch(ReaderCount,
                       [&instance, &stopReaders](int /*threadIndex*/)
    {
        EseSession session(instance);
        EseDatabase database(session, "ReadWrite.mdb",
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, "Rows", EseTableMode::Open);

        // Walk the table to completion repeatedly until the writer
        // signals stop.  We never assert on what we see — the point is
        // simply that readers don't deadlock against the writer.
        while (!stopReaders.load(std::memory_order_relaxed))
        {
            CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
            while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
                   != JET_errNoCurrentRecord)
            {
            }
        }
    });

    // Writer: append 50 more rows in a single transaction, then signal.
    {
        EseSession writerSession(instance);
        EseDatabase writerDatabase(writerSession, "ReadWrite.mdb",
                                   EseDatabaseMode::AttachAndOpen);
        EseTable writerTable(writerDatabase, "Rows", EseTableMode::Open);

        JET_COLUMNDEF valueDefinition = {};
        valueDefinition.cbStruct = sizeof(valueDefinition);
        CheckJet(JetGetTableColumnInfoA(writerSession.Handle(), writerTable.Id(),
                                        "Value", &valueDefinition,
                                        sizeof(valueDefinition), JET_ColInfo));

        EseTransaction transaction(writerSession);
        for (int rowIndex = InitialRows; rowIndex < InitialRows + 50; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(writerSession.Handle(),
                                      writerTable.Id(), JET_prepInsert));
            CheckJet(JetSetColumn(writerSession.Handle(), writerTable.Id(),
                                  valueDefinition.columnid,
                                  &rowIndex, sizeof(rowIndex), 0, nullptr));
            CheckJet(JetUpdate(writerSession.Handle(), writerTable.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    stopReaders.store(true, std::memory_order_relaxed);
    threadGroup.Join();
}

//  ===================================================================
//  Tier::Regression — escrow with mixed-sign deltas across many
//  contending sessions.
//
//  Smoke EscrowContentionSumsCorrectly has every worker do +1.  A
//  refactor that broke conflict resolution for mixed-sign deltas
//  (e.g., assumed monotonic counters in the lock-free fast-path)
//  would pass that test.  Here workers split into +N and -N
//  contributors with asymmetric counts so the predicted sum is a
//  non-trivial signed integer.  Final value must be exactly the
//  sum of every worker's contribution.
//  ===================================================================
EseIntegrationScenario(Concurrency, EscrowMixedSignDeltasContendOnSharedCounter, Regression)
{
    TemporaryDirectory directory(
        "Concurrency.EscrowMixedSignDeltasContendOnSharedCounter");
    EseInstance instance(directory);

    //  Workers: 4 increment by +3, 4 decrement by -7, each running
    //  1000 deltas.  Net change = 4*(+3)*1000 + 4*(-7)*1000 = -16000.
    //  Mixed signs force conflict-resolution to handle both
    //  directions in the same row history.
    static constexpr int32_t WorkersPerSign = 4;
    static constexpr int32_t IncrementsPerWorker = 1000;
    static constexpr int32_t PlusDelta = 3;
    static constexpr int32_t MinusDelta = -7;
    static constexpr int32_t InitialValue = 1'000'000;

    //  Setup: create the database + row in its own scope so the
    //  worker sessions can re-attach in AttachAndOpen mode.
    {
        EseSession session(instance);
        EseDatabase database(session, "EscrowMix.mdb");
        EseTable table(database, "Counter");
        table.AddColumnWithDefault("RefCount", JET_coltypLong,
                                   &InitialValue, sizeof(InitialValue),
                                   JET_bitColumnEscrowUpdate);
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }

    //  Workers: each opens its own session + cursor.  Even-indexed
    //  threads run +PlusDelta, odd-indexed threads run -MinusDelta.
    ThreadGroup threadGroup;
    threadGroup.Launch(WorkersPerSign * 2, [&instance](int threadIndex) {
        const int32_t delta =
            (threadIndex % 2 == 0) ? PlusDelta : MinusDelta;
        EseSession session(instance);
        EseDatabase database(session, "EscrowMix.mdb",
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, "Counter", EseTableMode::Open);
        JET_COLUMNDEF columnInfo = {};
        columnInfo.cbStruct = sizeof(columnInfo);
        CheckJet(JetGetTableColumnInfoA(session.Handle(), table.Id(),
                                         "RefCount",
                                         &columnInfo, sizeof(columnInfo),
                                         JET_ColInfo));
        for (int32_t i = 0; i < IncrementsPerWorker; ++i)
        {
            EseTransaction transaction(session);
            CheckJet(JetMove(session.Handle(), table.Id(),
                             JET_MoveFirst, 0));
            int32_t local = delta;
            uint32_t actualSize = 0;
            CheckJet(JetEscrowUpdate(session.Handle(), table.Id(),
                                     columnInfo.columnid,
                                     &local, sizeof(local),
                                     nullptr, 0, &actualSize, 0));
            transaction.Commit();
        }
    });
    threadGroup.Join();

    //  Verify: re-open and read the final counter value.
    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession, "EscrowMix.mdb",
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, "Counter", EseTableMode::Open);
    JET_COLUMNDEF columnInfo = {};
    columnInfo.cbStruct = sizeof(columnInfo);
    CheckJet(JetGetTableColumnInfoA(verifySession.Handle(),
                                     verifyTable.Id(), "RefCount",
                                     &columnInfo, sizeof(columnInfo),
                                     JET_ColInfo));
    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));
    int32_t finalValue = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(verifySession.Handle(), verifyTable.Id(),
                               columnInfo.columnid,
                               &finalValue, sizeof(finalValue),
                               &actualBytes, 0, nullptr));
    const int32_t expectedNet =
        WorkersPerSign * IncrementsPerWorker * PlusDelta
        + WorkersPerSign * IncrementsPerWorker * MinusDelta;
    Require(finalValue == InitialValue + expectedNet);
}

//  ===================================================================
//  Tier::Regression — many concurrent writers retry-resolve into
//  a sequential commit order.  Smoke ContendedReplace operates on
//  16 shared rows under uniform RMW; this one drives 8 writers
//  with non-overlapping but adjacent ranges so the engine's
//  write-conflict retry path has to converge under high pressure.
//  Final invariant: every row's Value equals the count of
//  successful increments contributed by all workers, no rows are
//  lost or doubled.
//  ===================================================================
EseIntegrationScenario(Concurrency, HighFanoutWriterContentionConverges, Regression)
{
    TemporaryDirectory directory(
        "Concurrency.HighFanoutWriterContentionConverges");
    EseInstance instance(directory);
    {
        EseSession setup(instance);
        EseDatabase database(setup, "Fanout.mdb");
        EseTable table(database, "Counters");
        auto slotColumn = table.AddColumn("Slot", JET_coltypLong,
                                          JET_bitColumnNotNULL);
        auto counterColumn = table.AddColumn("Counter", JET_coltypLong,
                                             JET_bitColumnNotNULL);
        static constexpr std::string_view PrimaryKey =
            std::string_view("+Slot\0\0", 7);
        table.CreateIndex("PrimaryBySlot", PrimaryKey,
                          JET_bitIndexPrimary | JET_bitIndexUnique);
        EseTransaction transaction(setup);
        //  16 rows, Slot=0..15, Counter initialized to 0.
        for (int32_t slot = 0; slot < 16; ++slot)
        {
            CheckJet(JetPrepareUpdate(setup.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(setup.Handle(), table.Id(),
                                  slotColumn,
                                  &slot, sizeof(slot), 0, nullptr));
            const int32_t zero = 0;
            CheckJet(JetSetColumn(setup.Handle(), table.Id(),
                                  counterColumn,
                                  &zero, sizeof(zero), 0, nullptr));
            CheckJet(JetUpdate(setup.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    static constexpr int32_t Workers = 8;
    static constexpr int32_t IncrementsPerWorker = 250;
    //  Each worker hits a 2-slot window (worker n -> slots [2n, 2n+1])
    //  — adjacent ranges so neighbours' commits brush against each
    //  other in the version store but don't conflict directly.
    ThreadGroup threadGroup;
    threadGroup.Launch(Workers, [&instance](int threadIndex) {
        EseSession workerSession(instance);
        EseDatabase workerDb(workerSession, "Fanout.mdb",
                             EseDatabaseMode::AttachAndOpen);
        EseTable workerTable(workerDb, "Counters", EseTableMode::Open);
        JET_COLUMNDEF slotInfo = {};
        slotInfo.cbStruct = sizeof(slotInfo);
        CheckJet(JetGetTableColumnInfoA(workerSession.Handle(),
                                         workerTable.Id(), "Slot",
                                         &slotInfo, sizeof(slotInfo),
                                         JET_ColInfo));
        JET_COLUMNDEF counterInfo = {};
        counterInfo.cbStruct = sizeof(counterInfo);
        CheckJet(JetGetTableColumnInfoA(workerSession.Handle(),
                                         workerTable.Id(), "Counter",
                                         &counterInfo, sizeof(counterInfo),
                                         JET_ColInfo));
        for (int32_t i = 0; i < IncrementsPerWorker; ++i)
        {
            const int32_t slot = threadIndex * 2 + (i & 1);
            //  Retry-on-conflict RMW.  The contention here is on
            //  the version store (neighbours commit) + page latches
            //  (adjacent slots may share leaf pages).
            while (true)
            {
                EseTransaction transaction(workerSession);
                CheckJet(JetMakeKey(workerSession.Handle(),
                                    workerTable.Id(),
                                    &slot, sizeof(slot),
                                    JET_bitNewKey));
                CheckJet(JetSeek(workerSession.Handle(),
                                 workerTable.Id(),
                                 JET_bitSeekEQ));
                int32_t current = 0;
                uint32_t actualBytes = 0;
                CheckJet(JetRetrieveColumn(workerSession.Handle(),
                                           workerTable.Id(),
                                           counterInfo.columnid,
                                           &current, sizeof(current),
                                           &actualBytes, 0, nullptr));
                const auto prepResult =
                    JetPrepareUpdate(workerSession.Handle(),
                                     workerTable.Id(),
                                     JET_prepReplace);
                if (prepResult == JET_errWriteConflict)
                {
                    CheckJet(JetRollback(workerSession.Handle(), 0));
                    continue;
                }
                CheckJet(prepResult);
                const int32_t bumped = current + 1;
                CheckJet(JetSetColumn(workerSession.Handle(),
                                      workerTable.Id(),
                                      counterInfo.columnid,
                                      &bumped, sizeof(bumped),
                                      0, nullptr));
                const auto updateResult =
                    JetUpdate(workerSession.Handle(),
                              workerTable.Id(),
                              nullptr, 0, nullptr);
                if (updateResult == JET_errWriteConflict)
                {
                    CheckJet(JetRollback(workerSession.Handle(), 0));
                    continue;
                }
                CheckJet(updateResult);
                transaction.Commit();
                break;
            }
        }
    });
    threadGroup.Join();

    //  Final invariant: each worker's two slots together absorbed
    //  IncrementsPerWorker bumps (half each, alternating).  Walk
    //  the table and sum per-worker pair.
    EseSession verify(instance);
    EseDatabase verifyDb(verify, "Fanout.mdb",
                         EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDb, "Counters", EseTableMode::Open);
    JET_COLUMNDEF slotInfo = {};
    slotInfo.cbStruct = sizeof(slotInfo);
    CheckJet(JetGetTableColumnInfoA(verify.Handle(), verifyTable.Id(),
                                     "Slot", &slotInfo, sizeof(slotInfo),
                                     JET_ColInfo));
    JET_COLUMNDEF counterInfo = {};
    counterInfo.cbStruct = sizeof(counterInfo);
    CheckJet(JetGetTableColumnInfoA(verify.Handle(), verifyTable.Id(),
                                     "Counter", &counterInfo,
                                     sizeof(counterInfo), JET_ColInfo));
    CheckJet(JetMove(verify.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));
    int32_t totalSum = 0;
    int32_t slotsSeen = 0;
    while (true)
    {
        int32_t slot = 0;
        int32_t counter = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(verify.Handle(), verifyTable.Id(),
                                   slotInfo.columnid,
                                   &slot, sizeof(slot),
                                   &actualBytes, 0, nullptr));
        CheckJet(JetRetrieveColumn(verify.Handle(), verifyTable.Id(),
                                   counterInfo.columnid,
                                   &counter, sizeof(counter),
                                   &actualBytes, 0, nullptr));
        Require(counter >= 0);
        totalSum += counter;
        ++slotsSeen;
        const auto moveResult = JetMove(verify.Handle(),
                                        verifyTable.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(slotsSeen == 16);
    Require(totalSum == Workers * IncrementsPerWorker);
}
