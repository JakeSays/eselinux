// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Long-running, two-instance variant. The engine has process-global
// state (the resource manager freezes after first use), but two
// JET_INSTANCE handles with disjoint SystemPath / LogFilePath /
// TempPath / BaseName are a supported topology and the one most
// applications hit when they keep "metadata" and "payload" stores in
// separate engines. This scenario hammers two such instances side by
// side from a single worker pool, then verifies each .edb in
// isolation. The verifier proves three things at once:
//   (1) writes destined for Instance A never leaked into Instance B
//       and vice versa,
//   (2) per-row checksums match the deterministic value derivation
//       (so no torn writes / off-by-one column copies snuck through),
//   (3) committed-insert minus committed-delete counters reconcile
//       against the surviving row count of each instance.

#include "Framework/Check.hxx"
#include "Framework/Console.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"
#include "Framework/ThreadGroup.hxx"

#include <jetapi.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace ese::tests;

namespace
{

constexpr uint32_t InstanceCount = 2;
constexpr uint32_t WorkersPerInstance = 8;
constexpr uint32_t TotalWorkerCount = InstanceCount * WorkersPerInstance;
constexpr std::chrono::seconds TimeBudget{120};

constexpr std::string_view DatabaseFileName = "TwoInstance.mdb";
constexpr std::string_view TableName = "Rows";

constexpr uint32_t MinimumBinaryBytes = 8 * 1024;
constexpr uint32_t MaximumBinaryBytes = 64 * 1024;
constexpr uint32_t MinimumTextBytes = 256;
constexpr uint32_t MaximumTextBytes = 4 * 1024;
constexpr uint16_t Codepage1252 = 1252;

constexpr uint32_t InsertWeight = 70;
constexpr uint32_t ReplaceWeight = 15;
constexpr uint32_t DeleteWeight = 10;
constexpr uint32_t SpotCheckWeight = 5;
constexpr uint32_t TotalWeight = InsertWeight + ReplaceWeight
                                  + DeleteWeight + SpotCheckWeight;
static_assert(TotalWeight == 100, "weights must sum to 100");

constexpr uint32_t InsertRollbackPercent = 10;

// Sample 1 in N rows during the post-run walk. The walk reads every
// payload from disk anyway (to count rows); checksum-verifying every
// 10th keeps the verifier sub-second on a couple-of-GB database.
constexpr uint32_t ProbeWalkStride = 10;

constexpr uint64_t Splitmix64Multiplier1 = 0xBF58476D1CE4E5B9ULL;
constexpr uint64_t Splitmix64Multiplier2 = 0x94D049BB133111EBULL;

// Mix InstanceIndex into the seed so the same (threadIndex,
// sequenceIndex) on Alpha and Beta yield different deterministic
// payloads. That makes a leaked write between instances detectable
// — the checksum on the wrong side won't match.
uint64_t MakeRowSeed(uint32_t instanceIndex,
                     uint32_t threadIndex,
                     uint64_t sequenceIndex)
{
    uint64_t mixed = (uint64_t{instanceIndex} << 56)
                     ^ (uint64_t{threadIndex} << 40)
                     ^ sequenceIndex;
    mixed = (mixed ^ (mixed >> 30)) * Splitmix64Multiplier1;
    mixed = (mixed ^ (mixed >> 27)) * Splitmix64Multiplier2;
    mixed = mixed ^ (mixed >> 31);
    return mixed;
}

struct AllColumnIds
{
    JET_COLUMNID ThreadIndex;
    JET_COLUMNID SequenceIndex;
    JET_COLUMNID SignedLong;
    JET_COLUMNID SignedLongLong;
    JET_COLUMNID DoublePrecision;
    JET_COLUMNID UniqueIdentifier;
    JET_COLUMNID BinaryPayload;
    JET_COLUMNID TextPayload;
    JET_COLUMNID Checksum;
    JET_COLUMNID ReplaceCounter;
};

struct RowValues
{
    int32_t SignedLong;
    int64_t SignedLongLong;
    double DoublePrecision;
    std::array<uint8_t, 16> UniqueIdentifier;
    std::vector<uint8_t> BinaryPayload;
    std::string TextPayload;
};

RowValues DeriveRowValues(uint32_t instanceIndex,
                          uint32_t threadIndex,
                          uint64_t sequenceIndex)
{
    std::mt19937_64 engine(MakeRowSeed(instanceIndex,
                                       threadIndex,
                                       sequenceIndex));

    RowValues values{};
    values.SignedLong = static_cast<int32_t>(engine() & 0xFFFFFFFFu);
    values.SignedLongLong = static_cast<int64_t>(engine());

    const uint64_t doubleBits = engine() & 0x7FEFFFFFFFFFFFFFULL;
    std::memcpy(&values.DoublePrecision, &doubleBits,
                sizeof(values.DoublePrecision));

    for (auto& byte : values.UniqueIdentifier)
    {
        byte = static_cast<uint8_t>(engine() & 0xFFu);
    }

    const uint32_t binaryBytes =
        MinimumBinaryBytes
        + static_cast<uint32_t>(
              engine() % (MaximumBinaryBytes - MinimumBinaryBytes + 1));
    values.BinaryPayload.resize(binaryBytes);
    for (auto& byte : values.BinaryPayload)
    {
        byte = static_cast<uint8_t>(engine() & 0xFFu);
    }

    constexpr char PrintableMin = 32;
    constexpr char PrintableMax = 126;
    constexpr int PrintableSpan = PrintableMax - PrintableMin + 1;
    const uint32_t textBytes =
        MinimumTextBytes
        + static_cast<uint32_t>(
              engine() % (MaximumTextBytes - MinimumTextBytes + 1));
    values.TextPayload.resize(textBytes);
    for (auto& character : values.TextPayload)
    {
        character = static_cast<char>(
            PrintableMin
            + static_cast<int>(engine() & 0xFFu) % PrintableSpan);
    }

    return values;
}

uint64_t ComputeChecksum(uint32_t instanceIndex,
                         uint32_t threadIndex,
                         uint64_t sequenceIndex,
                         const RowValues& values)
{
    constexpr uint64_t Fnv1aOffsetBasis = 0xCBF29CE484222325ULL;
    constexpr uint64_t Fnv1aPrime = 0x100000001B3ULL;

    uint64_t hash = Fnv1aOffsetBasis;
    auto fold = [&hash](const void* data, size_t bytes)
    {
        const auto* cursor = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < bytes; ++index)
        {
            hash ^= cursor[index];
            hash *= Fnv1aPrime;
        }
    };

    fold(&instanceIndex, sizeof(instanceIndex));
    fold(&threadIndex, sizeof(threadIndex));
    fold(&sequenceIndex, sizeof(sequenceIndex));
    fold(&values.SignedLong, sizeof(values.SignedLong));
    fold(&values.SignedLongLong, sizeof(values.SignedLongLong));
    fold(&values.DoublePrecision, sizeof(values.DoublePrecision));
    fold(values.UniqueIdentifier.data(), values.UniqueIdentifier.size());
    fold(values.BinaryPayload.data(), values.BinaryPayload.size());
    fold(values.TextPayload.data(), values.TextPayload.size());
    return hash;
}

void SetColumn(JET_SESID sessionHandle,
               JET_TABLEID tableId,
               JET_COLUMNID columnId,
               const void* data,
               uint32_t bytes)
{
    CheckJet(JetSetColumn(sessionHandle, tableId, columnId,
                          data, bytes, 0, nullptr));
}

void InsertFullRow(JET_SESID sessionHandle,
                   JET_TABLEID tableId,
                   const AllColumnIds& columnIds,
                   uint32_t threadIndex,
                   uint64_t sequenceIndex,
                   const RowValues& values,
                   uint64_t storedChecksum)
{
    CheckJet(JetPrepareUpdate(sessionHandle, tableId, JET_prepInsert));
    SetColumn(sessionHandle, tableId, columnIds.ThreadIndex,
              &threadIndex, sizeof(threadIndex));
    SetColumn(sessionHandle, tableId, columnIds.SequenceIndex,
              &sequenceIndex, sizeof(sequenceIndex));
    SetColumn(sessionHandle, tableId, columnIds.SignedLong,
              &values.SignedLong, sizeof(values.SignedLong));
    SetColumn(sessionHandle, tableId, columnIds.SignedLongLong,
              &values.SignedLongLong, sizeof(values.SignedLongLong));
    SetColumn(sessionHandle, tableId, columnIds.DoublePrecision,
              &values.DoublePrecision, sizeof(values.DoublePrecision));
    SetColumn(sessionHandle, tableId, columnIds.UniqueIdentifier,
              values.UniqueIdentifier.data(),
              static_cast<uint32_t>(values.UniqueIdentifier.size()));
    SetColumn(sessionHandle, tableId, columnIds.BinaryPayload,
              values.BinaryPayload.data(),
              static_cast<uint32_t>(values.BinaryPayload.size()));
    SetColumn(sessionHandle, tableId, columnIds.TextPayload,
              values.TextPayload.data(),
              static_cast<uint32_t>(values.TextPayload.size()));
    SetColumn(sessionHandle, tableId, columnIds.Checksum,
              &storedChecksum, sizeof(storedChecksum));
    const int64_t initialReplaceCounter = 0;
    SetColumn(sessionHandle, tableId, columnIds.ReplaceCounter,
              &initialReplaceCounter, sizeof(initialReplaceCounter));
    CheckJet(JetUpdate(sessionHandle, tableId, nullptr, 0, nullptr));
}

bool SeekRowByPrimaryKey(JET_SESID sessionHandle,
                         JET_TABLEID tableId,
                         uint32_t threadIndex,
                         uint64_t sequenceIndex)
{
    CheckJet(JetMakeKey(sessionHandle, tableId,
                        &threadIndex, sizeof(threadIndex),
                        JET_bitNewKey));
    CheckJet(JetMakeKey(sessionHandle, tableId,
                        &sequenceIndex, sizeof(sequenceIndex),
                        0));
    const JET_ERR seekError = JetSeek(sessionHandle, tableId,
                                       JET_bitSeekEQ);
    if (seekError == JET_errRecordNotFound)
    {
        return false;
    }
    CheckJet(seekError);
    return true;
}

void VerifyChecksumAgainstPrimaryKey(JET_SESID sessionHandle,
                                     JET_TABLEID tableId,
                                     const AllColumnIds& columnIds,
                                     uint32_t instanceIndex)
{
    uint32_t threadIndex = 0;
    uint64_t sequenceIndex = 0;
    int64_t storedChecksum = 0;
    uint32_t actualBytes = 0;

    CheckJet(JetRetrieveColumn(sessionHandle, tableId,
                               columnIds.ThreadIndex,
                               &threadIndex, sizeof(threadIndex),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == sizeof(threadIndex));

    CheckJet(JetRetrieveColumn(sessionHandle, tableId,
                               columnIds.SequenceIndex,
                               &sequenceIndex, sizeof(sequenceIndex),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == sizeof(sequenceIndex));

    CheckJet(JetRetrieveColumn(sessionHandle, tableId,
                               columnIds.Checksum,
                               &storedChecksum, sizeof(storedChecksum),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == sizeof(storedChecksum));

    const RowValues expectedValues =
        DeriveRowValues(instanceIndex, threadIndex, sequenceIndex);
    const uint64_t expectedChecksum =
        ComputeChecksum(instanceIndex, threadIndex,
                        sequenceIndex, expectedValues);
    Require(static_cast<uint64_t>(storedChecksum) == expectedChecksum);
}

enum class OperationKind
{
    Insert,
    Replace,
    Delete,
    SpotCheck,
};

OperationKind PickOperation(std::mt19937_64& engine)
{
    const uint32_t draw = static_cast<uint32_t>(engine() % TotalWeight);
    uint32_t cumulative = InsertWeight;
    if (draw < cumulative)
    {
        return OperationKind::Insert;
    }
    cumulative += ReplaceWeight;
    if (draw < cumulative)
    {
        return OperationKind::Replace;
    }
    cumulative += DeleteWeight;
    if (draw < cumulative)
    {
        return OperationKind::Delete;
    }
    return OperationKind::SpotCheck;
}

struct WorkerSummary
{
    uint64_t CommittedInserts = 0;
    uint64_t RolledBackInserts = 0;
    uint64_t CommittedReplaces = 0;
    uint64_t CommittedDeletes = 0;
    uint64_t SpotChecks = 0;
    uint64_t Iterations = 0;
};

// Build the shared schema on a single setup session per instance,
// capturing column ids for the workers to consume. Returns the
// column-id struct so worker threads avoid an extra
// JetGetTableColumnInfo round-trip on every iteration.
AllColumnIds CreateSchema(EseInstance& instance)
{
    AllColumnIds columnIds{};
    EseSession setupSession(instance);
    EseDatabase setupDatabase(setupSession, DatabaseFileName);
    EseTable setupTable(setupDatabase, TableName,
                        EseTableMode::Create,
                        /*initialPages*/ 128,
                        /*initialDensity*/ 80);

    columnIds.ThreadIndex = setupTable.AddColumn(
        "ThreadIndex", JET_coltypLong, JET_bitColumnNotNULL);
    columnIds.SequenceIndex = setupTable.AddColumn(
        "SequenceIndex", JET_coltypLongLong, JET_bitColumnNotNULL);
    columnIds.SignedLong = setupTable.AddColumn(
        "SignedLong", JET_coltypLong);
    columnIds.SignedLongLong = setupTable.AddColumn(
        "SignedLongLong", JET_coltypLongLong);
    columnIds.DoublePrecision = setupTable.AddColumn(
        "DoublePrecision", JET_coltypIEEEDouble);
    columnIds.UniqueIdentifier = setupTable.AddColumn(
        "UniqueIdentifier", JET_coltypGUID);
    columnIds.BinaryPayload = setupTable.AddColumn(
        "BinaryPayload", JET_coltypLongBinary);
    columnIds.TextPayload = setupTable.AddColumn(
        "TextPayload", JET_coltypLongText, 0, 0, Codepage1252);
    columnIds.Checksum = setupTable.AddColumn(
        "Checksum", JET_coltypLongLong, JET_bitColumnNotNULL);
    columnIds.ReplaceCounter = setupTable.AddColumn(
        "ReplaceCounter", JET_coltypLongLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKeyDescriptor =
        std::string_view("+ThreadIndex\0+SequenceIndex\0\0", 29);
    setupTable.CreateIndex("PrimaryByThreadAndSequence",
                           PrimaryKeyDescriptor,
                           JET_bitIndexPrimary);
    return columnIds;
}

// The per-worker insert/replace/delete/spot-check loop. Shared by
// both instances — they're parameterized by (instance handle,
// columnIds) so a single function body drives both. Returns the
// summary so the scenario can reconcile counts globally.
void DriveWorkload(EseInstance& instance,
                   const AllColumnIds& columnIds,
                   uint32_t instanceIndex,
                   uint32_t threadIndex,
                   std::chrono::steady_clock::time_point deadline,
                   WorkerSummary& summary)
{
    EseSession session(instance);
    EseDatabase database(session,
                         DatabaseFileName,
                         EseDatabaseMode::AttachAndOpen);
    EseTable table(database, TableName, EseTableMode::Open);

    std::mt19937_64 operationChooser(
        MakeRowSeed(instanceIndex, threadIndex, 0xC0DE));
    std::mt19937_64 rollbackChooser(
        MakeRowSeed(instanceIndex, threadIndex, 0xBEEF));
    std::mt19937_64 selectorChooser(
        MakeRowSeed(instanceIndex, threadIndex, 0xFEED));

    std::vector<uint64_t> committedSequences;
    committedSequences.reserve(1u << 13);

    uint64_t nextSequenceIndex = 0;

    while (std::chrono::steady_clock::now() < deadline)
    {
        const OperationKind operation = PickOperation(operationChooser);

        if (operation == OperationKind::Insert)
        {
            const uint64_t sequenceIndex = nextSequenceIndex++;
            const RowValues values =
                DeriveRowValues(instanceIndex, threadIndex, sequenceIndex);
            const uint64_t checksum =
                ComputeChecksum(instanceIndex, threadIndex,
                                sequenceIndex, values);

            EseTransaction transaction(session);
            InsertFullRow(session.Handle(), table.Id(), columnIds,
                          threadIndex, sequenceIndex, values, checksum);
            const bool shouldRollback =
                (rollbackChooser() % 100u) < InsertRollbackPercent;
            if (shouldRollback)
            {
                transaction.Rollback();
                ++summary.RolledBackInserts;
            }
            else
            {
                transaction.Commit();
                committedSequences.push_back(sequenceIndex);
                ++summary.CommittedInserts;
            }
        }
        else if (committedSequences.empty())
        {
            // No history yet — skip to the next iteration.
        }
        else if (operation == OperationKind::Replace)
        {
            const size_t index =
                static_cast<size_t>(selectorChooser()
                                    % committedSequences.size());
            const uint64_t targetSequence = committedSequences[index];

            EseTransaction transaction(session);
            if (!SeekRowByPrimaryKey(session.Handle(), table.Id(),
                                     threadIndex, targetSequence))
            {
                committedSequences[index] = committedSequences.back();
                committedSequences.pop_back();
                transaction.Rollback();
            }
            else
            {
                int64_t currentReplaceCounter = 0;
                uint32_t actualBytes = 0;
                CheckJet(JetRetrieveColumn(session.Handle(),
                                           table.Id(),
                                           columnIds.ReplaceCounter,
                                           &currentReplaceCounter,
                                           sizeof(currentReplaceCounter),
                                           &actualBytes, 0, nullptr));
                Require(actualBytes == sizeof(currentReplaceCounter));
                const int64_t bumpedCounter = currentReplaceCounter + 1;

                CheckJet(JetPrepareUpdate(session.Handle(),
                                          table.Id(),
                                          JET_prepReplace));
                SetColumn(session.Handle(), table.Id(),
                          columnIds.ReplaceCounter,
                          &bumpedCounter, sizeof(bumpedCounter));
                CheckJet(JetUpdate(session.Handle(), table.Id(),
                                   nullptr, 0, nullptr));
                transaction.Commit();
                ++summary.CommittedReplaces;
            }
        }
        else if (operation == OperationKind::Delete)
        {
            const size_t index =
                static_cast<size_t>(selectorChooser()
                                    % committedSequences.size());
            const uint64_t targetSequence = committedSequences[index];

            EseTransaction transaction(session);
            if (!SeekRowByPrimaryKey(session.Handle(), table.Id(),
                                     threadIndex, targetSequence))
            {
                transaction.Rollback();
            }
            else
            {
                CheckJet(JetDelete(session.Handle(), table.Id()));
                transaction.Commit();
                ++summary.CommittedDeletes;
            }
            committedSequences[index] = committedSequences.back();
            committedSequences.pop_back();
        }
        else if (operation == OperationKind::SpotCheck)
        {
            const size_t index =
                static_cast<size_t>(selectorChooser()
                                    % committedSequences.size());
            const uint64_t targetSequence = committedSequences[index];

            if (SeekRowByPrimaryKey(session.Handle(), table.Id(),
                                     threadIndex, targetSequence))
            {
                VerifyChecksumAgainstPrimaryKey(session.Handle(),
                                                table.Id(),
                                                columnIds,
                                                instanceIndex);
                ++summary.SpotChecks;
            }
            else
            {
                committedSequences[index] = committedSequences.back();
                committedSequences.pop_back();
            }
        }

        ++summary.Iterations;
    }
}

struct InstanceTotals
{
    uint64_t CommittedInserts = 0;
    uint64_t RolledBackInserts = 0;
    uint64_t CommittedReplaces = 0;
    uint64_t CommittedDeletes = 0;
    uint64_t SpotChecks = 0;
    uint64_t Iterations = 0;
};

void AccumulateInstance(InstanceTotals& totals,
                        const WorkerSummary& summary)
{
    totals.CommittedInserts += summary.CommittedInserts;
    totals.RolledBackInserts += summary.RolledBackInserts;
    totals.CommittedReplaces += summary.CommittedReplaces;
    totals.CommittedDeletes += summary.CommittedDeletes;
    totals.SpotChecks += summary.SpotChecks;
    totals.Iterations += summary.Iterations;
}

void VerifyInstanceContents(EseInstance& instance,
                            const AllColumnIds& columnIds,
                            uint32_t instanceIndex,
                            const InstanceTotals& totals,
                            std::string_view instanceLabel)
{
    const uint64_t expectedRowCount =
        totals.CommittedInserts - totals.CommittedDeletes;

    EseSession verifySession(instance);
    EseDatabase verifyDatabase(verifySession,
                               DatabaseFileName,
                               EseDatabaseMode::AttachAndOpen);
    EseTable verifyTable(verifyDatabase, TableName, EseTableMode::Open);

    CheckJet(JetMove(verifySession.Handle(), verifyTable.Id(),
                     JET_MoveFirst, 0));

    uint64_t observedRowCount = 0;
    uint64_t verifiedSampleCount = 0;
    while (true)
    {
        if ((observedRowCount % ProbeWalkStride) == 0)
        {
            VerifyChecksumAgainstPrimaryKey(verifySession.Handle(),
                                             verifyTable.Id(),
                                             columnIds,
                                             instanceIndex);
            ++verifiedSampleCount;
        }
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

    PrintLine("[ INFO ]   {}: iterations={}, committed-inserts={}, "
              "rolled-back={}, replaces={}, deletes={}, "
              "spot-checks={}, expected-rows={}, observed-rows={}, "
              "verified-samples={}",
              instanceLabel,
              totals.Iterations,
              totals.CommittedInserts,
              totals.RolledBackInserts,
              totals.CommittedReplaces,
              totals.CommittedDeletes,
              totals.SpotChecks,
              expectedRowCount,
              observedRowCount,
              verifiedSampleCount);

    Require(observedRowCount == expectedRowCount);
}

} // namespace

EseIntegrationScenario(LongRunning, TwoInstancesIndependentWorkloads)
{
    // Two completely independent EseInstance objects. Each gets its
    // own TemporaryDirectory so SystemPath/LogFilePath/TempPath don't
    // collide; the EventSource name doubles as a logical label.
    TemporaryDirectory alphaDirectory(
        "LongRunning.TwoInstancesIndependentWorkloads.Alpha");
    TemporaryDirectory betaDirectory(
        "LongRunning.TwoInstancesIndependentWorkloads.Beta");
    EseInstance alphaInstance(alphaDirectory,
                              "ese-tests-alpha",
                              /*runtimeCallback*/ nullptr,
                              EseInstanceMode::MultiInstance);
    EseInstance betaInstance(betaDirectory,
                             "ese-tests-beta",
                             /*runtimeCallback*/ nullptr,
                             EseInstanceMode::MultiInstance);

    const AllColumnIds alphaColumnIds = CreateSchema(alphaInstance);
    const AllColumnIds betaColumnIds = CreateSchema(betaInstance);

    const auto deadline = std::chrono::steady_clock::now() + TimeBudget;

    PrintLine("[ INFO ] LongRunning.TwoInstancesIndependentWorkloads "
              "running for {} s; {} workers/instance × {} instances",
              TimeBudget.count(), WorkersPerInstance, InstanceCount);

    std::vector<WorkerSummary> summaries(TotalWorkerCount);

    ThreadGroup workers;
    workers.Launch(static_cast<int>(TotalWorkerCount),
                   [&alphaInstance, &betaInstance,
                    &alphaColumnIds, &betaColumnIds,
                    &summaries, deadline]
                   (int workerIndexSigned)
    {
        const uint32_t workerIndex =
            static_cast<uint32_t>(workerIndexSigned);
        const uint32_t instanceIndex = workerIndex / WorkersPerInstance;
        const uint32_t threadIndex = workerIndex % WorkersPerInstance;

        EseInstance& instance =
            (instanceIndex == 0) ? alphaInstance : betaInstance;
        const AllColumnIds& columnIds =
            (instanceIndex == 0) ? alphaColumnIds : betaColumnIds;

        DriveWorkload(instance,
                      columnIds,
                      instanceIndex,
                      threadIndex,
                      deadline,
                      summaries[workerIndex]);
    });
    workers.Join();

    // Reconcile per-instance counters, then walk each .edb on its
    // own verify session. Each verifier asserts every sampled row's
    // checksum matches a re-derivation seeded with that instance's
    // index — a stray write that landed in the wrong instance would
    // fail the checksum on whichever side it ended up.
    InstanceTotals alphaTotals;
    InstanceTotals betaTotals;
    for (uint32_t workerIndex = 0;
         workerIndex < TotalWorkerCount;
         ++workerIndex)
    {
        const uint32_t instanceIndex = workerIndex / WorkersPerInstance;
        InstanceTotals& target =
            (instanceIndex == 0) ? alphaTotals : betaTotals;
        AccumulateInstance(target, summaries[workerIndex]);
    }

    VerifyInstanceContents(alphaInstance, alphaColumnIds, 0,
                            alphaTotals, "alpha");
    VerifyInstanceContents(betaInstance, betaColumnIds, 1,
                            betaTotals, "beta");
}
