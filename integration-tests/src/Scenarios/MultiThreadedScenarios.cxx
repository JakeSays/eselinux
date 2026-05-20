// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Multi-threaded stress scenarios. Each scenario drives a workload
// across several worker threads — concurrent transactions with mixed
// commit/rollback outcomes, large binary blobs, contended replaces
// with JET_errWriteConflict retry, nested savepoints, and concurrent
// delete-plus-insert churn — and then verifies every surviving record
// in the .edb against the deterministic value the workload was
// supposed to leave behind.
//
// Verification model:
//   Each (ThreadIndex, SequenceIndex[, TagByte]) tuple is hashed into
//   a 64-bit blob seed; the seed drives a mt19937_64 byte-stream that
//   fills the row's binary payload. After the workers join, a verify
//   session walks the entire table; for every observed row the test
//   regenerates the deterministic payload from its keys and compares
//   byte-for-byte against what ESE handed back. Row counts are
//   compared against the predicted committed set so spurious rows
//   (rolled-back transactions that leaked) or missing rows (committed
//   transactions that were dropped) both fail the scenario.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"
#include "Framework/ThreadGroup.hxx"

#include <jetapi.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace ese::tests;

namespace
{

// Identifiers for the four PK columns used across scenarios. Not every
// scenario uses all four — the column-id struct below carries only
// those each scenario builds.
constexpr std::string_view ThreadIndexColumnName = "ThreadIndex";
constexpr std::string_view SequenceIndexColumnName = "SequenceIndex";
constexpr std::string_view SavepointTagColumnName = "SavepointTag";
constexpr std::string_view GenerationColumnName = "Generation";
constexpr std::string_view PayloadColumnName = "Payload";
constexpr std::string_view PayloadBytesColumnName = "PayloadBytes";
constexpr std::string_view CounterColumnName = "Counter";

// Splitmix64 — collapses a (threadIndex, sequenceIndex[, tag]) tuple
// into a single 64-bit seed. Same inputs always produce the same
// output, so a verify session can regenerate any payload from PK
// alone. Distinct inputs scatter into well-separated streams.
constexpr uint64_t Splitmix64Multiplier1 = 0xBF58476D1CE4E5B9ULL;
constexpr uint64_t Splitmix64Multiplier2 = 0x94D049BB133111EBULL;

uint64_t MakeBlobSeed(uint32_t threadIndex,
                      uint32_t sequenceIndex,
                      uint32_t tag = 0)
{
    uint64_t mixedSeed = (uint64_t{threadIndex} << 32)
                         ^ (uint64_t{sequenceIndex} << 8)
                         ^ uint64_t{tag};
    mixedSeed = (mixedSeed ^ (mixedSeed >> 30)) * Splitmix64Multiplier1;
    mixedSeed = (mixedSeed ^ (mixedSeed >> 27)) * Splitmix64Multiplier2;
    mixedSeed = mixedSeed ^ (mixedSeed >> 31);
    return mixedSeed;
}

// Deterministic payload from a single seed. Used by both the worker
// (when writing) and the verify session (when checking). The size is
// also seeded so different rows get different lengths — that exercises
// both inline-tagged and long-value codepaths from one helper.
std::vector<uint8_t> DerivePayload(uint64_t blobSeed,
                                   uint32_t minimumBytes,
                                   uint32_t maximumBytes)
{
    std::mt19937_64 engine(blobSeed);
    const uint32_t sizeSpan = maximumBytes - minimumBytes + 1;
    const uint32_t payloadBytes =
        minimumBytes + static_cast<uint32_t>(engine() % sizeSpan);

    std::vector<uint8_t> payload(payloadBytes);
    for (auto& byte : payload)
    {
        byte = static_cast<uint8_t>(engine() & 0xFFu);
    }
    return payload;
}

// Helper that retrieves one fixed-width column from the current
// record. Used by verify sessions; throws on size mismatch so a wrong
// schema crashes loudly rather than silently truncating.
template <typename ValueType>
ValueType RetrieveFixedColumn(JET_SESID sessionHandle,
                              JET_TABLEID tableId,
                              JET_COLUMNID columnId)
{
    ValueType value = {};
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(sessionHandle,
                               tableId,
                               columnId,
                               &value,
                               sizeof(value),
                               &actualBytes,
                               0,
                               nullptr));
    Require(actualBytes == sizeof(value));
    return value;
}

// Helper that retrieves the entire payload column into a fresh
// vector. Calls JetRetrieveColumn twice — once to learn the size,
// once to fetch the bytes — so the buffer is exactly right-sized.
std::vector<uint8_t> RetrievePayloadColumn(JET_SESID sessionHandle,
                                           JET_TABLEID tableId,
                                           JET_COLUMNID payloadColumnId)
{
    uint32_t actualBytes = 0;
    const JET_ERR sizingError = JetRetrieveColumn(sessionHandle,
                                                  tableId,
                                                  payloadColumnId,
                                                  nullptr,
                                                  0,
                                                  &actualBytes,
                                                  0,
                                                  nullptr);
    // Zero-byte payload comes back as JET_errSuccess with actualBytes=0;
    // anything > 0 returns JET_wrnBufferTruncated (warning, not error)
    // because the destination buffer was null/short.
    if (sizingError != JET_errSuccess
        && sizingError != JET_wrnBufferTruncated
        && sizingError != JET_wrnColumnNull)
    {
        CheckJet(sizingError);
    }

    if (actualBytes == 0)
    {
        return {};
    }

    std::vector<uint8_t> payload(actualBytes);
    uint32_t fetchedBytes = 0;
    CheckJet(JetRetrieveColumn(sessionHandle,
                               tableId,
                               payloadColumnId,
                               payload.data(),
                               actualBytes,
                               &fetchedBytes,
                               0,
                               nullptr));
    Require(fetchedBytes == actualBytes);
    return payload;
}

// Insert one row with the typical schema (ThreadIndex/SequenceIndex/
// Payload) inside whatever transaction the caller is in.
void InsertTaggedRow(JET_SESID sessionHandle,
                     JET_TABLEID tableId,
                     JET_COLUMNID threadIndexColumnId,
                     JET_COLUMNID sequenceIndexColumnId,
                     JET_COLUMNID payloadColumnId,
                     uint32_t threadIndex,
                     uint32_t sequenceIndex,
                     const std::vector<uint8_t>& payload)
{
    CheckJet(JetPrepareUpdate(sessionHandle, tableId, JET_prepInsert));
    CheckJet(JetSetColumn(sessionHandle,
                          tableId,
                          threadIndexColumnId,
                          &threadIndex,
                          sizeof(threadIndex),
                          0,
                          nullptr));
    CheckJet(JetSetColumn(sessionHandle,
                          tableId,
                          sequenceIndexColumnId,
                          &sequenceIndex,
                          sizeof(sequenceIndex),
                          0,
                          nullptr));
    CheckJet(JetSetColumn(sessionHandle,
                          tableId,
                          payloadColumnId,
                          payload.data(),
                          uint32_t(payload.size()),
                          0,
                          nullptr));
    CheckJet(JetUpdate(sessionHandle, tableId, nullptr, 0, nullptr));
}

} // namespace

// =====================================================================
// Scenario 1 — mixed commit/rollback persistence
// =====================================================================
//
// Each worker runs `TransactionsPerWorker` transactions in sequence.
// Every transaction inserts `RowsPerTransaction` distinct rows. A
// deterministic policy decides which transactions commit and which
// roll back: transaction index `t` from worker `w` rolls back iff
// `(w + t) % RollbackEveryNth == 0`. The end-of-test verifier walks
// the entire table and proves the surviving set is exactly the
// committed rows — no rolled-back leakage, no committed rows missing.

EseIntegrationScenario(MultiThreaded, MixedCommitAndRollbackPersistsOnlyCommittedRows)
{
    TemporaryDirectory directory(
        "MultiThreaded.MixedCommitAndRollbackPersistsOnlyCommittedRows");
    EseInstance instance(directory);

    static constexpr uint32_t WorkerCount = 8;
    static constexpr uint32_t TransactionsPerWorker = 32;
    static constexpr uint32_t RowsPerTransaction = 16;
    static constexpr uint32_t RollbackEveryNth = 3;
    static constexpr uint32_t MinimumPayloadBytes = 8;
    static constexpr uint32_t MaximumPayloadBytes = 96;
    static constexpr std::string_view DatabaseFileName = "Mixed.mdb";
    static constexpr std::string_view TableName = "Rows";

    struct ColumnIds
    {
        JET_COLUMNID ThreadIndex;
        JET_COLUMNID SequenceIndex;
        JET_COLUMNID Payload;
    };
    ColumnIds columnIds = {};

    {
        EseSession setupSession(instance);
        EseDatabase setupDatabase(setupSession, DatabaseFileName);
        EseTable setupTable(setupDatabase, TableName);
        columnIds.ThreadIndex = setupTable.AddColumn(
            ThreadIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.SequenceIndex = setupTable.AddColumn(
            SequenceIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.Payload = setupTable.AddColumn(
            PayloadColumnName, JET_coltypLongBinary);

        static constexpr std::string_view PrimaryKeyDescriptor =
            std::string_view("+ThreadIndex\0+SequenceIndex\0\0", 29);
        setupTable.CreateIndex("PrimaryByThreadAndSequence",
                               PrimaryKeyDescriptor,
                               JET_bitIndexPrimary);
    }

    auto shouldRollback = [](uint32_t threadIndex, uint32_t transactionIndex)
    {
        return ((threadIndex + transactionIndex) % RollbackEveryNth) == 0;
    };

    ThreadGroup workers;
    workers.Launch(int(WorkerCount),
                   [&instance, columnIds, shouldRollback](int threadIndexSigned)
    {
        const uint32_t threadIndex = uint32_t(threadIndexSigned);

        EseSession session(instance);
        EseDatabase database(session,
                             DatabaseFileName,
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, TableName, EseTableMode::Open);

        for (uint32_t transactionIndex = 0;
             transactionIndex < TransactionsPerWorker;
             ++transactionIndex)
        {
            EseTransaction transaction(session);
            for (uint32_t rowIndex = 0;
                 rowIndex < RowsPerTransaction;
                 ++rowIndex)
            {
                const uint32_t sequenceIndex =
                    transactionIndex * RowsPerTransaction + rowIndex;
                const uint64_t blobSeed =
                    MakeBlobSeed(threadIndex, sequenceIndex);
                const auto payload = DerivePayload(blobSeed,
                                                   MinimumPayloadBytes,
                                                   MaximumPayloadBytes);
                InsertTaggedRow(session.Handle(),
                                table.Id(),
                                columnIds.ThreadIndex,
                                columnIds.SequenceIndex,
                                columnIds.Payload,
                                threadIndex,
                                sequenceIndex,
                                payload);
            }
            if (shouldRollback(threadIndex, transactionIndex))
            {
                transaction.Rollback();
            }
            else
            {
                transaction.Commit();
            }
        }
    });
    workers.Join();

    // Build the predicted committed set so verification can flag both
    // missing rows AND any leaked rows from rolled-back transactions.
    std::unordered_set<uint64_t> expectedRowKeys;
    for (uint32_t threadIndex = 0; threadIndex < WorkerCount; ++threadIndex)
    {
        for (uint32_t transactionIndex = 0;
             transactionIndex < TransactionsPerWorker;
             ++transactionIndex)
        {
            if (shouldRollback(threadIndex, transactionIndex))
            {
                continue;
            }
            for (uint32_t rowIndex = 0;
                 rowIndex < RowsPerTransaction;
                 ++rowIndex)
            {
                const uint32_t sequenceIndex =
                    transactionIndex * RowsPerTransaction + rowIndex;
                expectedRowKeys.insert(
                    (uint64_t{threadIndex} << 32) | uint64_t{sequenceIndex});
            }
        }
    }

    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession,
                               DatabaseFileName,
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, TableName, EseTableMode::Open);

    std::unordered_set<uint64_t> observedRowKeys;
    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));
    while (true)
    {
        const auto threadIndex = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.ThreadIndex);
        const auto sequenceIndex = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.SequenceIndex);
        const auto payload = RetrievePayloadColumn(
            verifySession.Handle(), verifyTable.Id(), columnIds.Payload);

        const uint64_t rowKey =
            (uint64_t{threadIndex} << 32) | uint64_t{sequenceIndex};
        Require(expectedRowKeys.contains(rowKey));
        Require(observedRowKeys.insert(rowKey).second);

        const uint64_t expectedSeed =
            MakeBlobSeed(threadIndex, sequenceIndex);
        const auto expectedPayload = DerivePayload(expectedSeed,
                                                    MinimumPayloadBytes,
                                                    MaximumPayloadBytes);
        Require(payload.size() == expectedPayload.size());
        Require(std::memcmp(payload.data(),
                            expectedPayload.data(),
                            payload.size()) == 0);

        const JET_ERR moveError = JetMove(verifySession.Handle(),
                                          verifyTable.Id(),
                                          JET_MoveNext, 0);
        if (moveError == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveError);
    }
    Require(observedRowKeys.size() == expectedRowKeys.size());
}

// =====================================================================
// Scenario 2 — large binary blobs round-trip across threads
// =====================================================================
//
// Workers insert rows whose payloads span well past one ESE page so
// the engine has to spill into long-value chunks. Sizes are seeded
// from the PK so they vary row-to-row (some workouts on the LV-tree
// fan-out, some single-chunk). Verify session reads every row back
// and byte-compares against the regenerated payload.

EseIntegrationScenario(MultiThreaded, LargeBinaryBlobsAcrossThreadsRoundTripExactly)
{
    TemporaryDirectory directory(
        "MultiThreaded.LargeBinaryBlobsAcrossThreadsRoundTripExactly");
    EseInstance instance(directory);

    static constexpr uint32_t WorkerCount = 8;
    static constexpr uint32_t RowsPerWorker = 12;
    // 16 KiB minimum guarantees every row spills out of inline tag
    // representation; 192 KiB maximum walks the LV-tree into a couple
    // of root branches at the engine's default page size.
    static constexpr uint32_t MinimumPayloadBytes = 16 * 1024;
    static constexpr uint32_t MaximumPayloadBytes = 192 * 1024;
    static constexpr std::string_view DatabaseFileName = "LargeBlobs.mdb";
    static constexpr std::string_view TableName = "Blobs";

    struct ColumnIds
    {
        JET_COLUMNID ThreadIndex;
        JET_COLUMNID SequenceIndex;
        JET_COLUMNID PayloadBytes;
        JET_COLUMNID Payload;
    };
    ColumnIds columnIds = {};

    {
        EseSession setupSession(instance);
        EseDatabase setupDatabase(setupSession, DatabaseFileName);
        EseTable setupTable(setupDatabase, TableName);
        columnIds.ThreadIndex = setupTable.AddColumn(
            ThreadIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.SequenceIndex = setupTable.AddColumn(
            SequenceIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.PayloadBytes = setupTable.AddColumn(
            PayloadBytesColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.Payload = setupTable.AddColumn(
            PayloadColumnName, JET_coltypLongBinary);

        static constexpr std::string_view PrimaryKeyDescriptor =
            std::string_view("+ThreadIndex\0+SequenceIndex\0\0", 29);
        setupTable.CreateIndex("PrimaryByThreadAndSequence",
                               PrimaryKeyDescriptor,
                               JET_bitIndexPrimary);
    }

    ThreadGroup workers;
    workers.Launch(int(WorkerCount),
                   [&instance, columnIds](int threadIndexSigned)
    {
        const uint32_t threadIndex = uint32_t(threadIndexSigned);

        EseSession session(instance);
        EseDatabase database(session,
                             DatabaseFileName,
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, TableName, EseTableMode::Open);

        // Each worker spreads its rows across several transactions so
        // we exercise commit boundaries while LVs are mid-flight. One
        // row per transaction keeps the engine doing real work on LV
        // tree pages instead of buffering everything in a single bulk
        // commit.
        for (uint32_t sequenceIndex = 0;
             sequenceIndex < RowsPerWorker;
             ++sequenceIndex)
        {
            const uint64_t blobSeed =
                MakeBlobSeed(threadIndex, sequenceIndex);
            const auto payload = DerivePayload(blobSeed,
                                                MinimumPayloadBytes,
                                                MaximumPayloadBytes);
            const uint32_t payloadBytes = uint32_t(payload.size());

            EseTransaction transaction(session);
            CheckJet(JetPrepareUpdate(session.Handle(),
                                      table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  columnIds.ThreadIndex,
                                  &threadIndex,
                                  sizeof(threadIndex),
                                  0,
                                  nullptr));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  columnIds.SequenceIndex,
                                  &sequenceIndex,
                                  sizeof(sequenceIndex),
                                  0,
                                  nullptr));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  columnIds.PayloadBytes,
                                  &payloadBytes,
                                  sizeof(payloadBytes),
                                  0,
                                  nullptr));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  columnIds.Payload,
                                  payload.data(),
                                  payloadBytes,
                                  0,
                                  nullptr));
            CheckJet(JetUpdate(session.Handle(),
                               table.Id(),
                               nullptr,
                               0,
                               nullptr));
            transaction.Commit();
        }
    });
    workers.Join();

    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession,
                               DatabaseFileName,
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, TableName, EseTableMode::Open);

    uint32_t observedRowCount = 0;
    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));
    while (true)
    {
        const auto threadIndex = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.ThreadIndex);
        const auto sequenceIndex = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.SequenceIndex);
        const auto recordedPayloadBytes = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.PayloadBytes);

        Require(threadIndex < WorkerCount);
        Require(sequenceIndex < RowsPerWorker);

        const uint64_t expectedSeed =
            MakeBlobSeed(threadIndex, sequenceIndex);
        const auto expectedPayload = DerivePayload(expectedSeed,
                                                    MinimumPayloadBytes,
                                                    MaximumPayloadBytes);
        Require(recordedPayloadBytes == expectedPayload.size());

        const auto payload = RetrievePayloadColumn(
            verifySession.Handle(), verifyTable.Id(), columnIds.Payload);
        Require(payload.size() == expectedPayload.size());
        Require(std::memcmp(payload.data(),
                            expectedPayload.data(),
                            payload.size()) == 0);
        ++observedRowCount;

        const JET_ERR moveError = JetMove(verifySession.Handle(),
                                          verifyTable.Id(),
                                          JET_MoveNext, 0);
        if (moveError == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveError);
    }
    Require(observedRowCount == WorkerCount * RowsPerWorker);
}

// =====================================================================
// Scenario 3 — contended replace, retry-until-applied semantics
// =====================================================================
//
// A pool of `SharedRowCount` rows is pre-seeded with Counter=0. Every
// worker repeatedly picks a random row, RMW-increments its counter,
// and commits. When the engine surfaces JET_errWriteConflict the
// worker rolls back and retries; the test asserts every increment
// eventually applies, so the final sum across all rows equals the
// total number of attempts.

EseIntegrationScenario(MultiThreaded, ContendedReplaceRetriesUntilEveryIncrementApplies)
{
    TemporaryDirectory directory(
        "MultiThreaded.ContendedReplaceRetriesUntilEveryIncrementApplies");
    EseInstance instance(directory);

    static constexpr uint32_t WorkerCount = 8;
    static constexpr uint32_t SharedRowCount = 16;
    static constexpr uint32_t IncrementsPerWorker = 200;
    static constexpr int64_t InitialCounterValue = 0;
    static constexpr std::string_view DatabaseFileName = "Contended.mdb";
    static constexpr std::string_view TableName = "Rows";

    struct ColumnIds
    {
        JET_COLUMNID SequenceIndex;
        JET_COLUMNID Counter;
    };
    ColumnIds columnIds = {};

    {
        EseSession setupSession(instance);
        EseDatabase setupDatabase(setupSession, DatabaseFileName);
        EseTable setupTable(setupDatabase, TableName);
        columnIds.SequenceIndex = setupTable.AddColumn(
            SequenceIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.Counter = setupTable.AddColumn(
            CounterColumnName, JET_coltypLongLong, JET_bitColumnNotNULL);

        static constexpr std::string_view PrimaryKeyDescriptor =
            std::string_view("+SequenceIndex\0\0", 16);
        setupTable.CreateIndex("PrimaryBySequence",
                               PrimaryKeyDescriptor,
                               JET_bitIndexPrimary);

        EseTransaction transaction(setupSession);
        for (uint32_t sequenceIndex = 0;
             sequenceIndex < SharedRowCount;
             ++sequenceIndex)
        {
            CheckJet(JetPrepareUpdate(setupSession.Handle(),
                                      setupTable.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(setupSession.Handle(),
                                  setupTable.Id(),
                                  columnIds.SequenceIndex,
                                  &sequenceIndex,
                                  sizeof(sequenceIndex),
                                  0,
                                  nullptr));
            const int64_t initialCounter = InitialCounterValue;
            CheckJet(JetSetColumn(setupSession.Handle(),
                                  setupTable.Id(),
                                  columnIds.Counter,
                                  &initialCounter,
                                  sizeof(initialCounter),
                                  0,
                                  nullptr));
            CheckJet(JetUpdate(setupSession.Handle(),
                               setupTable.Id(),
                               nullptr,
                               0,
                               nullptr));
        }
        transaction.Commit();
    }

    ThreadGroup workers;
    workers.Launch(int(WorkerCount),
                   [&instance, columnIds](int threadIndexSigned)
    {
        const uint32_t threadIndex = uint32_t(threadIndexSigned);

        EseSession session(instance);
        EseDatabase database(session,
                             DatabaseFileName,
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, TableName, EseTableMode::Open);

        // Per-worker deterministic stream of target rows so a failure
        // can be reproduced from the worker seed alone.
        std::mt19937_64 rowChooser(MakeBlobSeed(threadIndex, 0xCAFE));

        for (uint32_t attemptIndex = 0;
             attemptIndex < IncrementsPerWorker;
             ++attemptIndex)
        {
            const uint32_t targetSequence =
                uint32_t(rowChooser() % SharedRowCount);
            while (true)
            {
                CheckJet(JetBeginTransaction2(session.Handle(), 0));
                bool retryRequired = false;

                CheckJet(JetMakeKey(session.Handle(),
                                    table.Id(),
                                    &targetSequence,
                                    sizeof(targetSequence),
                                    JET_bitNewKey));
                CheckJet(JetSeek(session.Handle(),
                                 table.Id(),
                                 JET_bitSeekEQ));

                int64_t currentCounter = 0;
                uint32_t actualBytes = 0;
                CheckJet(JetRetrieveColumn(session.Handle(),
                                           table.Id(),
                                           columnIds.Counter,
                                           &currentCounter,
                                           sizeof(currentCounter),
                                           &actualBytes,
                                           0,
                                           nullptr));
                Require(actualBytes == sizeof(currentCounter));

                const int64_t newCounter = currentCounter + 1;
                JET_ERR prepareError = JetPrepareUpdate(session.Handle(),
                                                        table.Id(),
                                                        JET_prepReplace);
                if (prepareError == JET_errWriteConflict)
                {
                    retryRequired = true;
                }
                else
                {
                    CheckJet(prepareError);
                    JET_ERR setError = JetSetColumn(session.Handle(),
                                                    table.Id(),
                                                    columnIds.Counter,
                                                    &newCounter,
                                                    sizeof(newCounter),
                                                    0,
                                                    nullptr);
                    if (setError == JET_errWriteConflict)
                    {
                        retryRequired = true;
                    }
                    else
                    {
                        CheckJet(setError);
                        JET_ERR updateError = JetUpdate(session.Handle(),
                                                        table.Id(),
                                                        nullptr,
                                                        0,
                                                        nullptr);
                        if (updateError == JET_errWriteConflict)
                        {
                            retryRequired = true;
                        }
                        else
                        {
                            CheckJet(updateError);
                        }
                    }
                }

                if (retryRequired)
                {
                    (void)JetRollback(session.Handle(), 0);
                    // Tiny pause so the conflicting peer gets a chance
                    // to finish its commit. Pure spinning starves the
                    // peer and stretches scenario runtime.
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(10));
                    continue;
                }

                JET_ERR commitError = JetCommitTransaction(session.Handle(),
                                                           0);
                if (commitError == JET_errWriteConflict)
                {
                    (void)JetRollback(session.Handle(), 0);
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(10));
                    continue;
                }
                CheckJet(commitError);
                break;
            }
        }
    });
    workers.Join();

    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession,
                               DatabaseFileName,
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, TableName, EseTableMode::Open);

    int64_t observedTotal = 0;
    uint32_t observedRowCount = 0;
    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));
    while (true)
    {
        const auto sequenceIndex = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.SequenceIndex);
        const auto counter = RetrieveFixedColumn<int64_t>(
            verifySession.Handle(), verifyTable.Id(), columnIds.Counter);
        Require(sequenceIndex < SharedRowCount);
        Require(counter >= 0);
        observedTotal += counter;
        ++observedRowCount;

        const JET_ERR moveError = JetMove(verifySession.Handle(),
                                          verifyTable.Id(),
                                          JET_MoveNext, 0);
        if (moveError == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveError);
    }
    Require(observedRowCount == SharedRowCount);
    Require(observedTotal == int64_t{WorkerCount} * int64_t{IncrementsPerWorker});
}

// =====================================================================
// Scenario 4 — nested savepoints, mixed inner outcomes
// =====================================================================
//
// Every worker runs `OuterTransactionsPerWorker` outer transactions.
// Inside each outer, three savepoints (A/B/C) each insert one row;
// savepoint A commits, savepoint B rolls back, savepoint C commits.
// The outer transaction always commits. After all workers finish, the
// verifier asserts that exactly the A and C rows survive, none of the
// B rows do, and every payload is byte-correct.

EseIntegrationScenario(MultiThreaded, NestedSavepointMixHonorsInnerRollback)
{
    TemporaryDirectory directory(
        "MultiThreaded.NestedSavepointMixHonorsInnerRollback");
    EseInstance instance(directory);

    static constexpr uint32_t WorkerCount = 6;
    static constexpr uint32_t OuterTransactionsPerWorker = 40;
    static constexpr uint32_t MinimumPayloadBytes = 32;
    static constexpr uint32_t MaximumPayloadBytes = 256;
    static constexpr uint8_t TagA = 'A';
    static constexpr uint8_t TagB = 'B';
    static constexpr uint8_t TagC = 'C';
    static constexpr std::string_view DatabaseFileName = "Savepoints.mdb";
    static constexpr std::string_view TableName = "Rows";

    struct ColumnIds
    {
        JET_COLUMNID ThreadIndex;
        JET_COLUMNID SequenceIndex;
        JET_COLUMNID SavepointTag;
        JET_COLUMNID Payload;
    };
    ColumnIds columnIds = {};

    {
        EseSession setupSession(instance);
        EseDatabase setupDatabase(setupSession, DatabaseFileName);
        EseTable setupTable(setupDatabase, TableName);
        columnIds.ThreadIndex = setupTable.AddColumn(
            ThreadIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.SequenceIndex = setupTable.AddColumn(
            SequenceIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.SavepointTag = setupTable.AddColumn(
            SavepointTagColumnName, JET_coltypUnsignedByte, JET_bitColumnNotNULL);
        columnIds.Payload = setupTable.AddColumn(
            PayloadColumnName, JET_coltypLongBinary);

        static constexpr std::string_view PrimaryKeyDescriptor =
            std::string_view("+ThreadIndex\0+SequenceIndex\0+SavepointTag\0\0",
                             43);
        setupTable.CreateIndex("PrimaryByThreadSequenceTag",
                               PrimaryKeyDescriptor,
                               JET_bitIndexPrimary);
    }

    auto insertTaggedPayload = [&](JET_SESID sessionHandle,
                                   JET_TABLEID tableId,
                                   uint32_t threadIndex,
                                   uint32_t sequenceIndex,
                                   uint8_t tag)
    {
        const uint64_t blobSeed =
            MakeBlobSeed(threadIndex, sequenceIndex, uint32_t{tag});
        const auto payload = DerivePayload(blobSeed,
                                            MinimumPayloadBytes,
                                            MaximumPayloadBytes);
        CheckJet(JetPrepareUpdate(sessionHandle, tableId, JET_prepInsert));
        CheckJet(JetSetColumn(sessionHandle, tableId,
                              columnIds.ThreadIndex,
                              &threadIndex, sizeof(threadIndex),
                              0, nullptr));
        CheckJet(JetSetColumn(sessionHandle, tableId,
                              columnIds.SequenceIndex,
                              &sequenceIndex, sizeof(sequenceIndex),
                              0, nullptr));
        CheckJet(JetSetColumn(sessionHandle, tableId,
                              columnIds.SavepointTag,
                              &tag, sizeof(tag),
                              0, nullptr));
        CheckJet(JetSetColumn(sessionHandle, tableId,
                              columnIds.Payload,
                              payload.data(), uint32_t(payload.size()),
                              0, nullptr));
        CheckJet(JetUpdate(sessionHandle, tableId, nullptr, 0, nullptr));
    };

    ThreadGroup workers;
    workers.Launch(int(WorkerCount),
                   [&instance, columnIds, insertTaggedPayload](int threadIndexSigned)
    {
        (void)columnIds;
        const uint32_t threadIndex = uint32_t(threadIndexSigned);

        EseSession session(instance);
        EseDatabase database(session,
                             DatabaseFileName,
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, TableName, EseTableMode::Open);

        for (uint32_t transactionIndex = 0;
             transactionIndex < OuterTransactionsPerWorker;
             ++transactionIndex)
        {
            EseTransaction outer(session);

            // Savepoint A — commits and is promoted into the outer.
            {
                EseTransaction inner(session);
                insertTaggedPayload(session.Handle(), table.Id(),
                                    threadIndex, transactionIndex, TagA);
                inner.Commit();
            }
            // Savepoint B — rolls back; the row must not survive.
            {
                EseTransaction inner(session);
                insertTaggedPayload(session.Handle(), table.Id(),
                                    threadIndex, transactionIndex, TagB);
                inner.Rollback();
            }
            // Savepoint C — commits, promoted into the outer.
            {
                EseTransaction inner(session);
                insertTaggedPayload(session.Handle(), table.Id(),
                                    threadIndex, transactionIndex, TagC);
                inner.Commit();
            }

            outer.Commit();
        }
    });
    workers.Join();

    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession,
                               DatabaseFileName,
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, TableName, EseTableMode::Open);

    uint32_t observedRowCount = 0;
    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));
    while (true)
    {
        const auto threadIndex = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.ThreadIndex);
        const auto sequenceIndex = RetrieveFixedColumn<uint32_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.SequenceIndex);
        const auto tag = RetrieveFixedColumn<uint8_t>(
            verifySession.Handle(), verifyTable.Id(),
            columnIds.SavepointTag);

        Require(threadIndex < WorkerCount);
        Require(sequenceIndex < OuterTransactionsPerWorker);
        // Tag B rows were inserted under savepoints that rolled back —
        // they must never reach the database.
        Require(tag == TagA || tag == TagC);

        const uint64_t expectedSeed =
            MakeBlobSeed(threadIndex, sequenceIndex, uint32_t{tag});
        const auto expectedPayload = DerivePayload(expectedSeed,
                                                    MinimumPayloadBytes,
                                                    MaximumPayloadBytes);
        const auto payload = RetrievePayloadColumn(
            verifySession.Handle(), verifyTable.Id(), columnIds.Payload);
        Require(payload.size() == expectedPayload.size());
        Require(std::memcmp(payload.data(),
                            expectedPayload.data(),
                            payload.size()) == 0);
        ++observedRowCount;

        const JET_ERR moveError = JetMove(verifySession.Handle(),
                                          verifyTable.Id(),
                                          JET_MoveNext, 0);
        if (moveError == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveError);
    }
    // Two surviving tags (A, C) per outer transaction per worker.
    static constexpr uint32_t SurvivingTagsPerTransaction = 2;
    Require(observedRowCount
            == WorkerCount * OuterTransactionsPerWorker
               * SurvivingTagsPerTransaction);
}

// =====================================================================
// Scenario 5 — concurrent delete + insert churn
// =====================================================================
//
// Phase 1 seeds Generation 0 rows. Phase 2 spawns two pools of
// workers: deleters tear out a known subset of Generation 0 rows, and
// inserters add fresh Generation 1 rows in parallel. Because deleters
// only touch Generation 0 and inserters only write Generation 1,
// there's no write conflict between the pools — they still race
// against the engine's page allocator and B-tree merges. Verification
// re-derives the expected survivor set from the deletion rule and the
// inserter schedule, then walks the entire .edb.

EseIntegrationScenario(MultiThreaded, DeleteAndInsertChurnAcrossThreadsPreservesInvariant)
{
    TemporaryDirectory directory(
        "MultiThreaded.DeleteAndInsertChurnAcrossThreadsPreservesInvariant");
    EseInstance instance(directory);

    static constexpr uint32_t DeleterCount = 4;
    static constexpr uint32_t InserterCount = 4;
    static constexpr uint32_t SeededRowCount = 512;
    static constexpr uint32_t InsertsPerInserter = 64;
    static constexpr uint32_t MinimumPayloadBytes = 32;
    static constexpr uint32_t MaximumPayloadBytes = 1024;
    static constexpr uint32_t Generation0 = 0;
    static constexpr uint32_t Generation1 = 1;
    static constexpr uint32_t Generation0SeedingThread = 0;
    // Deleter `d` removes the rows whose sequence falls in its slice
    // of the lower half. Upper half always survives.
    static constexpr uint32_t SliceUpperBound = SeededRowCount / 2;
    static constexpr uint32_t SliceWidth = SliceUpperBound / DeleterCount;
    static_assert(SliceUpperBound % DeleterCount == 0,
                  "deleter slice must divide evenly");

    static constexpr std::string_view DatabaseFileName = "Churn.mdb";
    static constexpr std::string_view TableName = "Rows";

    struct ColumnIds
    {
        JET_COLUMNID Generation;
        JET_COLUMNID ThreadIndex;
        JET_COLUMNID SequenceIndex;
        JET_COLUMNID Payload;
    };
    ColumnIds columnIds = {};

    {
        EseSession setupSession(instance);
        EseDatabase setupDatabase(setupSession, DatabaseFileName);
        EseTable setupTable(setupDatabase, TableName);
        columnIds.Generation = setupTable.AddColumn(
            GenerationColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.ThreadIndex = setupTable.AddColumn(
            ThreadIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.SequenceIndex = setupTable.AddColumn(
            SequenceIndexColumnName, JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.Payload = setupTable.AddColumn(
            PayloadColumnName, JET_coltypLongBinary);

        static constexpr std::string_view PrimaryKeyDescriptor =
            std::string_view("+Generation\0+ThreadIndex\0+SequenceIndex\0\0",
                             41);
        setupTable.CreateIndex("PrimaryByGenerationThreadSequence",
                               PrimaryKeyDescriptor,
                               JET_bitIndexPrimary);

        // Phase 1 — seed Generation 0. All seeded rows live under a
        // single thread index so deleters can identify them by
        // (Generation=0, ThreadIndex=0) and split sequence space.
        EseTransaction transaction(setupSession);
        for (uint32_t sequenceIndex = 0;
             sequenceIndex < SeededRowCount;
             ++sequenceIndex)
        {
            const uint64_t blobSeed =
                MakeBlobSeed(Generation0SeedingThread,
                             sequenceIndex,
                             Generation0);
            const auto payload = DerivePayload(blobSeed,
                                                MinimumPayloadBytes,
                                                MaximumPayloadBytes);
            CheckJet(JetPrepareUpdate(setupSession.Handle(),
                                      setupTable.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(setupSession.Handle(),
                                  setupTable.Id(),
                                  columnIds.Generation,
                                  &Generation0,
                                  sizeof(Generation0),
                                  0,
                                  nullptr));
            CheckJet(JetSetColumn(setupSession.Handle(),
                                  setupTable.Id(),
                                  columnIds.ThreadIndex,
                                  &Generation0SeedingThread,
                                  sizeof(Generation0SeedingThread),
                                  0,
                                  nullptr));
            CheckJet(JetSetColumn(setupSession.Handle(),
                                  setupTable.Id(),
                                  columnIds.SequenceIndex,
                                  &sequenceIndex,
                                  sizeof(sequenceIndex),
                                  0,
                                  nullptr));
            CheckJet(JetSetColumn(setupSession.Handle(),
                                  setupTable.Id(),
                                  columnIds.Payload,
                                  payload.data(),
                                  uint32_t(payload.size()),
                                  0,
                                  nullptr));
            CheckJet(JetUpdate(setupSession.Handle(),
                               setupTable.Id(),
                               nullptr,
                               0,
                               nullptr));
        }
        transaction.Commit();
    }

    // Phase 2 — deleters and inserters run in parallel under a single
    // ThreadGroup so the engine sees both populations interleaved.
    ThreadGroup churn;
    churn.Launch(int(DeleterCount + InserterCount),
                 [&instance, columnIds](int workerIndexSigned)
    {
        const uint32_t workerIndex = uint32_t(workerIndexSigned);
        const bool isDeleter = workerIndex < DeleterCount;

        EseSession session(instance);
        EseDatabase database(session,
                             DatabaseFileName,
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, TableName, EseTableMode::Open);

        if (isDeleter)
        {
            const uint32_t deleterIndex = workerIndex;
            const uint32_t sliceStart = deleterIndex * SliceWidth;
            const uint32_t sliceEnd = sliceStart + SliceWidth;

            EseTransaction transaction(session);
            for (uint32_t sequenceIndex = sliceStart;
                 sequenceIndex < sliceEnd;
                 ++sequenceIndex)
            {
                struct
                {
                    uint32_t Generation;
                    uint32_t ThreadIndex;
                    uint32_t SequenceIndex;
                } primaryKey =
                {
                    Generation0,
                    Generation0SeedingThread,
                    sequenceIndex,
                };
                CheckJet(JetMakeKey(session.Handle(), table.Id(),
                                    &primaryKey.Generation,
                                    sizeof(primaryKey.Generation),
                                    JET_bitNewKey));
                CheckJet(JetMakeKey(session.Handle(), table.Id(),
                                    &primaryKey.ThreadIndex,
                                    sizeof(primaryKey.ThreadIndex),
                                    0));
                CheckJet(JetMakeKey(session.Handle(), table.Id(),
                                    &primaryKey.SequenceIndex,
                                    sizeof(primaryKey.SequenceIndex),
                                    0));
                CheckJet(JetSeek(session.Handle(), table.Id(),
                                 JET_bitSeekEQ));
                CheckJet(JetDelete(session.Handle(), table.Id()));
            }
            transaction.Commit();
        }
        else
        {
            const uint32_t inserterIndex = workerIndex - DeleterCount;

            // Inserters fan their work across multiple transactions so
            // commits land while deleters are still active.
            static constexpr uint32_t InsertsPerTransaction = 8;
            uint32_t sequenceIndex = 0;
            while (sequenceIndex < InsertsPerInserter)
            {
                const uint32_t batchEnd = std::min(
                    sequenceIndex + InsertsPerTransaction,
                    InsertsPerInserter);
                EseTransaction transaction(session);
                while (sequenceIndex < batchEnd)
                {
                    const uint64_t blobSeed =
                        MakeBlobSeed(inserterIndex,
                                     sequenceIndex,
                                     Generation1);
                    const auto payload = DerivePayload(blobSeed,
                                                        MinimumPayloadBytes,
                                                        MaximumPayloadBytes);
                    CheckJet(JetPrepareUpdate(session.Handle(),
                                              table.Id(),
                                              JET_prepInsert));
                    CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                          columnIds.Generation,
                                          &Generation1,
                                          sizeof(Generation1),
                                          0, nullptr));
                    CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                          columnIds.ThreadIndex,
                                          &inserterIndex,
                                          sizeof(inserterIndex),
                                          0, nullptr));
                    CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                          columnIds.SequenceIndex,
                                          &sequenceIndex,
                                          sizeof(sequenceIndex),
                                          0, nullptr));
                    CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                          columnIds.Payload,
                                          payload.data(),
                                          uint32_t(payload.size()),
                                          0, nullptr));
                    CheckJet(JetUpdate(session.Handle(), table.Id(),
                                       nullptr, 0, nullptr));
                    ++sequenceIndex;
                }
                transaction.Commit();
            }
        }
    });
    churn.Join();

    // Build the expected survivor set deterministically from the
    // deletion rule (Generation 0 rows in [SliceUpperBound,
    // SeededRowCount) survive; everything in [0, SliceUpperBound) is
    // deleted) and the inserter schedule (every inserter writes
    // [0, InsertsPerInserter)).
    struct RowKey
    {
        uint32_t Generation;
        uint32_t ThreadIndex;
        uint32_t SequenceIndex;
        bool operator==(const RowKey& other) const = default;
    };
    struct RowKeyHash
    {
        size_t operator()(const RowKey& key) const noexcept
        {
            return size_t(MakeBlobSeed(key.ThreadIndex,
                                       key.SequenceIndex,
                                       key.Generation));
        }
    };

    std::unordered_set<RowKey, RowKeyHash> expectedRowKeys;
    for (uint32_t sequenceIndex = SliceUpperBound;
         sequenceIndex < SeededRowCount;
         ++sequenceIndex)
    {
        expectedRowKeys.insert({Generation0,
                                Generation0SeedingThread,
                                sequenceIndex});
    }
    for (uint32_t inserterIndex = 0;
         inserterIndex < InserterCount;
         ++inserterIndex)
    {
        for (uint32_t sequenceIndex = 0;
             sequenceIndex < InsertsPerInserter;
             ++sequenceIndex)
        {
            expectedRowKeys.insert({Generation1,
                                    inserterIndex,
                                    sequenceIndex});
        }
    }

    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession,
                               DatabaseFileName,
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, TableName, EseTableMode::Open);

    std::unordered_set<RowKey, RowKeyHash> observedRowKeys;
    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));
    while (true)
    {
        RowKey rowKey =
        {
            RetrieveFixedColumn<uint32_t>(verifySession.Handle(),
                                          verifyTable.Id(),
                                          columnIds.Generation),
            RetrieveFixedColumn<uint32_t>(verifySession.Handle(),
                                          verifyTable.Id(),
                                          columnIds.ThreadIndex),
            RetrieveFixedColumn<uint32_t>(verifySession.Handle(),
                                          verifyTable.Id(),
                                          columnIds.SequenceIndex),
        };
        Require(expectedRowKeys.contains(rowKey));
        Require(observedRowKeys.insert(rowKey).second);

        const uint64_t expectedSeed = MakeBlobSeed(rowKey.ThreadIndex,
                                                    rowKey.SequenceIndex,
                                                    rowKey.Generation);
        const auto expectedPayload = DerivePayload(expectedSeed,
                                                    MinimumPayloadBytes,
                                                    MaximumPayloadBytes);
        const auto payload = RetrievePayloadColumn(
            verifySession.Handle(), verifyTable.Id(), columnIds.Payload);
        Require(payload.size() == expectedPayload.size());
        Require(std::memcmp(payload.data(),
                            expectedPayload.data(),
                            payload.size()) == 0);

        const JET_ERR moveError = JetMove(verifySession.Handle(),
                                          verifyTable.Id(),
                                          JET_MoveNext, 0);
        if (moveError == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveError);
    }
    Require(observedRowKeys.size() == expectedRowKeys.size());
}
