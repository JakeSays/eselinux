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

EseIntegrationScenario(Concurrency, ThreadGroupLaunchAndJoin)
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

EseIntegrationScenario(Concurrency, MultiSessionParallelInsertsAllSucceed)
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

EseIntegrationScenario(Concurrency, EscrowContentionSumsCorrectly)
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

EseIntegrationScenario(Concurrency, ConcurrentReadersDontBlockWriter)
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
