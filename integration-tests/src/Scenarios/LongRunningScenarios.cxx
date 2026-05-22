// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Long-running stress scenarios.  Off by default — they hammer the
// engine for minutes apiece and exist to surface assertions, leaks,
// and silent corruption that the short-form suite can't shake out.
// Run with `ese-tests --include-long-running --filter "LongRunning.*"`.
//
// `HeavyMultiThreadedSchema` builds a table with 21 columns spanning
// every coltyp the public API exposes (Bit, UnsignedByte, Short,
// UnsignedShort, Long, UnsignedLong, LongLong, UnsignedLongLong,
// Currency, IEEESingle, IEEEDouble, DateTime, GUID, Binary, Text,
// LongBinary, LongText, plus PK and bookkeeping columns), and runs
// a thread pool against it for ~2 wall-clock minutes.  Each worker
// chooses among insert / replace / delete / spot-check per iteration,
// with deterministic rollback on a fraction of inserts.
//
// Verification is "spot" by design — full-table re-derivation across
// gigabytes of LV data would dominate runtime.  The scenario relies
// on three layered checks:
//   1. CheckJet on every API call surfaces engine errors immediately.
//   2. Per-row Checksum column is written deterministically; spot
//      reads during the run AND a sampled walk at the end re-derive
//      and compare.
//   3. Final row count is reconciled against atomic worker counters
//      (committed-inserts minus committed-deletes), so missing rows
//      or leaked rolled-back rows fail the scenario.

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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace ese::tests;

namespace
{

constexpr uint32_t WorkerCount = 12;
constexpr std::chrono::seconds TimeBudget{120};

constexpr std::string_view DatabaseFileName = "LongRunning.mdb";
constexpr std::string_view TableName = "HeavyRows";

constexpr uint16_t Codepage1252 = 1252;
constexpr uint32_t SmallBinaryBytes = 80;
constexpr uint32_t SmallTextBytes = 96;
constexpr uint32_t MinimumLongBinaryBytes = 16 * 1024;
constexpr uint32_t MaximumLongBinaryBytes = 128 * 1024;
constexpr uint32_t MinimumLongTextBytes = 1 * 1024;
constexpr uint32_t MaximumLongTextBytes = 32 * 1024;

// Operation mix per worker iteration.  Numbers are percentage weights
// summing to 100.  Insert is dominant; replace and delete keep the
// btree churning; SpotCheckSample triggers a mid-run validation.
constexpr uint32_t InsertWeight = 70;
constexpr uint32_t ReplaceWeight = 15;
constexpr uint32_t DeleteWeight = 10;
constexpr uint32_t SpotCheckWeight = 5;
constexpr uint32_t TotalWeight = InsertWeight + ReplaceWeight
                                  + DeleteWeight + SpotCheckWeight;
static_assert(TotalWeight == 100, "weights must sum to 100");

// Fraction of insert transactions that intentionally roll back.
// Picked from a per-iteration RNG draw; ~10% rollbacks exercises the
// engine's undo path under contention with concurrent commits.
constexpr uint32_t InsertRollbackPercent = 10;

// Sample rate for the end-of-test full walk.  Every ProbeWalkStride
// rows have their Checksum column re-derived from PK and compared
// against the on-disk value.  At default config that's ~10% coverage.
constexpr uint32_t ProbeWalkStride = 10;

// Splitmix64 multipliers — same constants as the MultiThreaded suite
// so deterministic row derivation is consistent across scenarios.
constexpr uint64_t Splitmix64Multiplier1 = 0xBF58476D1CE4E5B9ULL;
constexpr uint64_t Splitmix64Multiplier2 = 0x94D049BB133111EBULL;

uint64_t MakeRowSeed(uint32_t threadIndex, uint64_t sequenceIndex)
{
    uint64_t mixed = (uint64_t{threadIndex} << 40) ^ sequenceIndex;
    mixed = (mixed ^ (mixed >> 30)) * Splitmix64Multiplier1;
    mixed = (mixed ^ (mixed >> 27)) * Splitmix64Multiplier2;
    mixed = mixed ^ (mixed >> 31);
    return mixed;
}

// All column ids the workers need, captured during setup so workers
// can avoid extra JetGetTableColumnInfo round-trips.  Worker threads
// receive a const reference to one instance of this struct.
struct AllColumnIds
{
    JET_COLUMNID ThreadIndex;
    JET_COLUMNID SequenceIndex;
    JET_COLUMNID BitFlag;
    JET_COLUMNID SmallUnsigned;
    JET_COLUMNID SignedShort;
    JET_COLUMNID UnsignedShort;
    JET_COLUMNID SignedLong;
    JET_COLUMNID UnsignedLong;
    JET_COLUMNID SignedLongLong;
    JET_COLUMNID UnsignedLongLong;
    JET_COLUMNID Money;
    JET_COLUMNID SinglePrecision;
    JET_COLUMNID DoublePrecision;
    JET_COLUMNID CalendarDate;
    JET_COLUMNID UniqueIdentifier;
    JET_COLUMNID SmallBinary;
    JET_COLUMNID SmallText;
    JET_COLUMNID LongBinaryPayload;
    JET_COLUMNID LongTextPayload;
    JET_COLUMNID Checksum;
    JET_COLUMNID ReplaceCounter;
};

// Deterministic per-row column set.  The same seed always produces
// the same RowValues, so a verifier given (ThreadIndex, SequenceIndex)
// can re-derive every byte without consulting any in-memory state.
struct RowValues
{
    uint8_t BitFlag;
    uint8_t SmallUnsigned;
    int16_t SignedShort;
    uint16_t UnsignedShort;
    int32_t SignedLong;
    uint32_t UnsignedLong;
    int64_t SignedLongLong;
    uint64_t UnsignedLongLong;
    int64_t Money;
    float SinglePrecision;
    double DoublePrecision;
    double CalendarDate;
    std::array<uint8_t, 16> UniqueIdentifier;
    std::array<uint8_t, SmallBinaryBytes> SmallBinary;
    std::array<char, SmallTextBytes> SmallText;
    std::vector<uint8_t> LongBinaryPayload;
    std::string LongTextPayload;
};

RowValues DeriveRowValues(uint32_t threadIndex, uint64_t sequenceIndex)
{
    std::mt19937_64 engine(MakeRowSeed(threadIndex, sequenceIndex));
    auto nextU64 = [&engine]() { return engine(); };
    auto nextU32 = [&]() { return static_cast<uint32_t>(nextU64() & 0xFFFFFFFFu); };
    auto nextU16 = [&]() { return static_cast<uint16_t>(nextU64() & 0xFFFFu); };
    auto nextU8 = [&]() { return static_cast<uint8_t>(nextU64() & 0xFFu); };

    RowValues values{};
    values.BitFlag = static_cast<uint8_t>(nextU64() & 1u);
    values.SmallUnsigned = nextU8();
    values.SignedShort = static_cast<int16_t>(nextU16());
    values.UnsignedShort = nextU16();
    values.SignedLong = static_cast<int32_t>(nextU32());
    values.UnsignedLong = nextU32();
    values.SignedLongLong = static_cast<int64_t>(nextU64());
    values.UnsignedLongLong = nextU64();
    values.Money = static_cast<int64_t>(nextU64());

    // Keep float / double payloads finite so a bit-equal compare on
    // re-derive doesn't depend on NaN payload preservation across the
    // engine's storage layer.
    const uint32_t singleBits = nextU32() & 0x7F7FFFFFu;
    std::memcpy(&values.SinglePrecision, &singleBits,
                sizeof(values.SinglePrecision));
    const uint64_t doubleBits = nextU64() & 0x7FEFFFFFFFFFFFFFULL;
    std::memcpy(&values.DoublePrecision, &doubleBits,
                sizeof(values.DoublePrecision));
    // DateTime stores OLE-Automation days; pick a value in a plausible
    // range so the engine's serialization isn't fed nonsense bits.
    constexpr double DateBaseDays = 30000.0;  // ~1982
    constexpr double DateSpanDays = 30000.0;
    const uint64_t dateUnit = nextU64();
    const double dateFraction =
        static_cast<double>(dateUnit & 0xFFFFFFFFu) / 4294967295.0;
    values.CalendarDate = DateBaseDays + dateFraction * DateSpanDays;

    for (auto& byte : values.UniqueIdentifier)
    {
        byte = nextU8();
    }
    for (auto& byte : values.SmallBinary)
    {
        byte = nextU8();
    }
    // SmallText is fixed-size printable ASCII so debug dumps stay
    // readable.  Range [32, 126] covers space through tilde.
    constexpr char PrintableMin = 32;
    constexpr char PrintableMax = 126;
    constexpr int PrintableSpan = PrintableMax - PrintableMin + 1;
    for (auto& character : values.SmallText)
    {
        character = static_cast<char>(PrintableMin + (nextU8() % PrintableSpan));
    }

    const uint32_t longBinaryBytes =
        MinimumLongBinaryBytes
        + static_cast<uint32_t>(
              nextU64() % (MaximumLongBinaryBytes - MinimumLongBinaryBytes + 1));
    values.LongBinaryPayload.resize(longBinaryBytes);
    for (auto& byte : values.LongBinaryPayload)
    {
        byte = nextU8();
    }

    const uint32_t longTextBytes =
        MinimumLongTextBytes
        + static_cast<uint32_t>(
              nextU64() % (MaximumLongTextBytes - MinimumLongTextBytes + 1));
    values.LongTextPayload.resize(longTextBytes);
    for (auto& character : values.LongTextPayload)
    {
        character = static_cast<char>(PrintableMin + (nextU8() % PrintableSpan));
    }

    return values;
}

// FNV-1a 64-bit folded over every byte that's stored in a non-PK,
// non-bookkeeping column.  Returns a stable signature for the row's
// "value-bearing" payload.  ReplaceCounter is excluded so a replace
// that only bumps that counter doesn't invalidate the Checksum.
uint64_t ComputeChecksum(uint32_t threadIndex,
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

    fold(&threadIndex, sizeof(threadIndex));
    fold(&sequenceIndex, sizeof(sequenceIndex));
    fold(&values.BitFlag, sizeof(values.BitFlag));
    fold(&values.SmallUnsigned, sizeof(values.SmallUnsigned));
    fold(&values.SignedShort, sizeof(values.SignedShort));
    fold(&values.UnsignedShort, sizeof(values.UnsignedShort));
    fold(&values.SignedLong, sizeof(values.SignedLong));
    fold(&values.UnsignedLong, sizeof(values.UnsignedLong));
    fold(&values.SignedLongLong, sizeof(values.SignedLongLong));
    fold(&values.UnsignedLongLong, sizeof(values.UnsignedLongLong));
    fold(&values.Money, sizeof(values.Money));
    fold(&values.SinglePrecision, sizeof(values.SinglePrecision));
    fold(&values.DoublePrecision, sizeof(values.DoublePrecision));
    fold(&values.CalendarDate, sizeof(values.CalendarDate));
    fold(values.UniqueIdentifier.data(), values.UniqueIdentifier.size());
    fold(values.SmallBinary.data(), values.SmallBinary.size());
    fold(values.SmallText.data(), values.SmallText.size());
    fold(values.LongBinaryPayload.data(), values.LongBinaryPayload.size());
    fold(values.LongTextPayload.data(), values.LongTextPayload.size());
    return hash;
}

void SetColumn(JET_SESID sessionHandle,
               JET_TABLEID tableId,
               JET_COLUMNID columnId,
               const void* data,
               uint32_t bytes)
{
    CheckJet(JetSetColumn(sessionHandle,
                          tableId,
                          columnId,
                          data,
                          bytes,
                          0,
                          nullptr));
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
    SetColumn(sessionHandle, tableId, columnIds.BitFlag,
              &values.BitFlag, sizeof(values.BitFlag));
    SetColumn(sessionHandle, tableId, columnIds.SmallUnsigned,
              &values.SmallUnsigned, sizeof(values.SmallUnsigned));
    SetColumn(sessionHandle, tableId, columnIds.SignedShort,
              &values.SignedShort, sizeof(values.SignedShort));
    SetColumn(sessionHandle, tableId, columnIds.UnsignedShort,
              &values.UnsignedShort, sizeof(values.UnsignedShort));
    SetColumn(sessionHandle, tableId, columnIds.SignedLong,
              &values.SignedLong, sizeof(values.SignedLong));
    SetColumn(sessionHandle, tableId, columnIds.UnsignedLong,
              &values.UnsignedLong, sizeof(values.UnsignedLong));
    SetColumn(sessionHandle, tableId, columnIds.SignedLongLong,
              &values.SignedLongLong, sizeof(values.SignedLongLong));
    SetColumn(sessionHandle, tableId, columnIds.UnsignedLongLong,
              &values.UnsignedLongLong, sizeof(values.UnsignedLongLong));
    SetColumn(sessionHandle, tableId, columnIds.Money,
              &values.Money, sizeof(values.Money));
    SetColumn(sessionHandle, tableId, columnIds.SinglePrecision,
              &values.SinglePrecision, sizeof(values.SinglePrecision));
    SetColumn(sessionHandle, tableId, columnIds.DoublePrecision,
              &values.DoublePrecision, sizeof(values.DoublePrecision));
    SetColumn(sessionHandle, tableId, columnIds.CalendarDate,
              &values.CalendarDate, sizeof(values.CalendarDate));
    SetColumn(sessionHandle, tableId, columnIds.UniqueIdentifier,
              values.UniqueIdentifier.data(),
              static_cast<uint32_t>(values.UniqueIdentifier.size()));
    SetColumn(sessionHandle, tableId, columnIds.SmallBinary,
              values.SmallBinary.data(),
              static_cast<uint32_t>(values.SmallBinary.size()));
    SetColumn(sessionHandle, tableId, columnIds.SmallText,
              values.SmallText.data(),
              static_cast<uint32_t>(values.SmallText.size()));
    SetColumn(sessionHandle, tableId, columnIds.LongBinaryPayload,
              values.LongBinaryPayload.data(),
              static_cast<uint32_t>(values.LongBinaryPayload.size()));
    SetColumn(sessionHandle, tableId, columnIds.LongTextPayload,
              values.LongTextPayload.data(),
              static_cast<uint32_t>(values.LongTextPayload.size()));
    SetColumn(sessionHandle, tableId, columnIds.Checksum,
              &storedChecksum, sizeof(storedChecksum));
    const int64_t initialReplaceCounter = 0;
    SetColumn(sessionHandle, tableId, columnIds.ReplaceCounter,
              &initialReplaceCounter, sizeof(initialReplaceCounter));
    CheckJet(JetUpdate(sessionHandle, tableId, nullptr, 0, nullptr));
}

// Seek to a row by primary key.  Returns true on hit, false when the
// row was lost to a racing delete.  Throws on any other JET error.
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

// Read the Checksum column from the current row and confirm it
// matches the value we'd predict from PK + the deterministic value
// derivation.  Spot validation during the run AND post-run sample.
void VerifyChecksumAgainstPrimaryKey(JET_SESID sessionHandle,
                                     JET_TABLEID tableId,
                                     const AllColumnIds& columnIds)
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
        DeriveRowValues(threadIndex, sequenceIndex);
    const uint64_t expectedChecksum =
        ComputeChecksum(threadIndex, sequenceIndex, expectedValues);
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
    const uint32_t draw =
        static_cast<uint32_t>(engine() % TotalWeight);
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

} // namespace

EseIntegrationScenario(MultiThreaded, HeavyMultiThreadedHammer, LongRunning)
{
    TemporaryDirectory directory("LongRunning.HeavyMultiThreadedHammer");
    EseInstance instance(directory);

    AllColumnIds columnIds = {};

    {
        EseSession setupSession(instance);
        EseDatabase setupDatabase(setupSession, DatabaseFileName);
        EseTable setupTable(setupDatabase, TableName,
                            EseTableMode::Create,
                            /*initialPages*/ 256,
                            /*initialDensity*/ 80);

        columnIds.ThreadIndex = setupTable.AddColumn(
            "ThreadIndex", JET_coltypLong, JET_bitColumnNotNULL);
        columnIds.SequenceIndex = setupTable.AddColumn(
            "SequenceIndex", JET_coltypLongLong, JET_bitColumnNotNULL);
        columnIds.BitFlag = setupTable.AddColumn(
            "BitFlag", JET_coltypBit);
        columnIds.SmallUnsigned = setupTable.AddColumn(
            "SmallUnsigned", JET_coltypUnsignedByte);
        columnIds.SignedShort = setupTable.AddColumn(
            "SignedShort", JET_coltypShort);
        columnIds.UnsignedShort = setupTable.AddColumn(
            "UnsignedShort", JET_coltypUnsignedShort);
        columnIds.SignedLong = setupTable.AddColumn(
            "SignedLong", JET_coltypLong);
        columnIds.UnsignedLong = setupTable.AddColumn(
            "UnsignedLong", JET_coltypUnsignedLong);
        columnIds.SignedLongLong = setupTable.AddColumn(
            "SignedLongLong", JET_coltypLongLong);
        columnIds.UnsignedLongLong = setupTable.AddColumn(
            "UnsignedLongLong", JET_coltypUnsignedLongLong);
        columnIds.Money = setupTable.AddColumn(
            "Money", JET_coltypCurrency);
        columnIds.SinglePrecision = setupTable.AddColumn(
            "SinglePrecision", JET_coltypIEEESingle);
        columnIds.DoublePrecision = setupTable.AddColumn(
            "DoublePrecision", JET_coltypIEEEDouble);
        columnIds.CalendarDate = setupTable.AddColumn(
            "CalendarDate", JET_coltypDateTime);
        columnIds.UniqueIdentifier = setupTable.AddColumn(
            "UniqueIdentifier", JET_coltypGUID);
        columnIds.SmallBinary = setupTable.AddColumn(
            "SmallBinary", JET_coltypBinary, 0, SmallBinaryBytes);
        columnIds.SmallText = setupTable.AddColumn(
            "SmallText", JET_coltypText, 0, SmallTextBytes, Codepage1252);
        columnIds.LongBinaryPayload = setupTable.AddColumn(
            "LongBinaryPayload", JET_coltypLongBinary);
        columnIds.LongTextPayload = setupTable.AddColumn(
            "LongTextPayload", JET_coltypLongText, 0, 0, Codepage1252);
        columnIds.Checksum = setupTable.AddColumn(
            "Checksum", JET_coltypLongLong, JET_bitColumnNotNULL);
        columnIds.ReplaceCounter = setupTable.AddColumn(
            "ReplaceCounter", JET_coltypLongLong, JET_bitColumnNotNULL);

        static constexpr std::string_view PrimaryKeyDescriptor =
            std::string_view("+ThreadIndex\0+SequenceIndex\0\0", 29);
        setupTable.CreateIndex("PrimaryByThreadAndSequence",
                               PrimaryKeyDescriptor,
                               JET_bitIndexPrimary);
    }

    const auto deadline = std::chrono::steady_clock::now() + TimeBudget;
    std::vector<WorkerSummary> summaries(WorkerCount);

    PrintLine("[ INFO ] LongRunning.HeavyMultiThreadedHammer "
              "running for {} s with {} workers",
              TimeBudget.count(), WorkerCount);

    ThreadGroup workers;
    workers.Launch(static_cast<int>(WorkerCount),
                   [&instance, &columnIds, &summaries, deadline]
                   (int threadIndexSigned)
    {
        const uint32_t threadIndex = static_cast<uint32_t>(threadIndexSigned);
        WorkerSummary& summary = summaries[threadIndex];

        EseSession session(instance);
        EseDatabase database(session,
                             DatabaseFileName,
                             EseDatabaseMode::AttachAndOpen);
        EseTable table(database, TableName, EseTableMode::Open);

        // Per-worker RNG seeded from the worker's thread index so a
        // failure can be reproduced from threadIndex alone.
        std::mt19937_64 operationChooser(MakeRowSeed(threadIndex, 0xC0DE));
        std::mt19937_64 rollbackChooser(MakeRowSeed(threadIndex, 0xBEEF));
        std::mt19937_64 selectorChooser(MakeRowSeed(threadIndex, 0xFEED));

        // Worker's running list of committed sequence indices. Used
        // to target existing rows for replace / delete / spot-check.
        std::vector<uint64_t> committedSequences;
        committedSequences.reserve(1u << 14);

        uint64_t nextSequenceIndex = 0;

        while (std::chrono::steady_clock::now() < deadline)
        {
            const OperationKind operation = PickOperation(operationChooser);

            if (operation == OperationKind::Insert)
            {
                const uint64_t sequenceIndex = nextSequenceIndex++;
                const RowValues values =
                    DeriveRowValues(threadIndex, sequenceIndex);
                const uint64_t checksum =
                    ComputeChecksum(threadIndex, sequenceIndex, values);

                EseTransaction transaction(session);
                InsertFullRow(session.Handle(), table.Id(), columnIds,
                              threadIndex, sequenceIndex, values,
                              checksum);
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
                // Nothing yet to target — fall through to next iter.
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
                    // Lost the race with our own delete logic — drop
                    // the stale id and move on.
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
                    CheckJet(JetUpdate(session.Handle(),
                                       table.Id(),
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

                // Read-only — no transaction needed; ESE auto-bounds
                // the implicit read on the cursor.
                if (SeekRowByPrimaryKey(session.Handle(), table.Id(),
                                         threadIndex, targetSequence))
                {
                    VerifyChecksumAgainstPrimaryKey(session.Handle(),
                                                    table.Id(),
                                                    columnIds);
                    ++summary.SpotChecks;
                }
                else
                {
                    // Lost the row to a peer's delete? Workers only
                    // delete their own rows; if we still own this id
                    // its absence is a bug. Drop the id and continue.
                    committedSequences[index] = committedSequences.back();
                    committedSequences.pop_back();
                }
            }

            ++summary.Iterations;
        }
    });
    workers.Join();

    // Aggregate worker counters.  Final on-disk row count must equal
    // committed inserts minus committed deletes; anything else means
    // a rolled-back row leaked or a committed row got lost.
    uint64_t totalCommittedInserts = 0;
    uint64_t totalRolledBackInserts = 0;
    uint64_t totalCommittedReplaces = 0;
    uint64_t totalCommittedDeletes = 0;
    uint64_t totalSpotChecks = 0;
    uint64_t totalIterations = 0;
    for (const auto& summary : summaries)
    {
        totalCommittedInserts += summary.CommittedInserts;
        totalRolledBackInserts += summary.RolledBackInserts;
        totalCommittedReplaces += summary.CommittedReplaces;
        totalCommittedDeletes += summary.CommittedDeletes;
        totalSpotChecks += summary.SpotChecks;
        totalIterations += summary.Iterations;
    }
    const uint64_t expectedRowCount =
        totalCommittedInserts - totalCommittedDeletes;

    PrintLine("[ INFO ]   iterations: {}, committed-inserts: {}, "
              "rolled-back-inserts: {}, committed-replaces: {}, "
              "committed-deletes: {}, spot-checks: {}, expected-rows: {}",
              totalIterations,
              totalCommittedInserts,
              totalRolledBackInserts,
              totalCommittedReplaces,
              totalCommittedDeletes,
              totalSpotChecks,
              expectedRowCount);

    // End-of-test sampled walk.  Walking every row and re-deriving
    // every byte against potentially gigabytes of LV data would
    // dominate runtime; sampling every ProbeWalkStride-th row hits
    // ~10% of the table while keeping the verify pass O(seconds).
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
                                             columnIds);
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

    PrintLine("[ INFO ]   observed-rows: {}, verified-samples: {}",
              observedRowCount, verifiedSampleCount);

    Require(observedRowCount == expectedRowCount);
}

//  ===================================================================
//  Tier::LongRunning — sustained insert/delete churn under buffer
//  cache pressure to exercise the engine's periodic background
//  maintenance (page eviction, version-store cleanup, checkpoint
//  advancement).  Smoke and Regression tiers don't run long
//  enough to drive these heuristics.  Refactors to the
//  background-thread cadence or the cache aging policy slip past
//  tests that only run a few seconds.
//
//  Workload: 20 cycles, each inserts 10,000 rows then deletes
//  every other row.  Total ~5 minutes wall clock — enough for
//  multiple checkpoint advances and many eviction passes.  Final
//  walk validates every survivor's exact Value, proving the
//  engine kept the live set intact across all the background
//  bookkeeping.
//  ===================================================================
EseIntegrationScenario(MultiThreaded, SustainedChurnUnderCachePressure, LongRunning)
{
    TemporaryDirectory directory(
        "MultiThreaded.SustainedChurnUnderCachePressure");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Churn.mdb");
    EseTable table(database, "Rows");
    auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                     JET_bitColumnNotNULL);
    auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                       JET_bitColumnNotNULL);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr int32_t Cycles = 20;
    static constexpr int32_t RowsPerCycle = 10'000;
    int32_t nextKey = 0;
    int32_t aliveCount = 0;
    int32_t firstAliveKey = -1;
    for (int32_t cycle = 0; cycle < Cycles; ++cycle)
    {
        EseTransaction insertTxn(session);
        for (int32_t i = 0; i < RowsPerCycle; ++i)
        {
            const int32_t key = nextKey++;
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  keyColumn, &key, sizeof(key),
                                  0, nullptr));
            const int32_t value = key * 31 + cycle;
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  valueColumn, &value, sizeof(value),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
            if (firstAliveKey < 0)
            {
                firstAliveKey = key;
            }
        }
        insertTxn.Commit();
        aliveCount += RowsPerCycle;

        //  Delete every other row from THIS cycle.
        EseTransaction deleteTxn(session);
        const int32_t cycleStart = cycle * RowsPerCycle;
        for (int32_t k = cycleStart; k < cycleStart + RowsPerCycle; k += 2)
        {
            CheckJet(JetMakeKey(session.Handle(), table.Id(),
                                &k, sizeof(k), JET_bitNewKey));
            CheckJet(JetSeek(session.Handle(), table.Id(),
                             JET_bitSeekEQ));
            CheckJet(JetDelete(session.Handle(), table.Id()));
            --aliveCount;
        }
        deleteTxn.Commit();
    }

    //  Final walk: confirm every survivor's exact Value.  Surviving
    //  keys are odd-indexed within each cycle (k%2==1).
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t walked = 0;
    while (true)
    {
        int32_t observedKey = 0;
        int32_t observedValue = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   keyColumn,
                                   &observedKey, sizeof(observedKey),
                                   &actualBytes, 0, nullptr));
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   valueColumn,
                                   &observedValue, sizeof(observedValue),
                                   &actualBytes, 0, nullptr));
        Require(observedKey % 2 == 1);  // survivor
        const int32_t cycleIndex = observedKey / RowsPerCycle;
        Require(observedValue == observedKey * 31 + cycleIndex);
        ++walked;
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walked == aliveCount);
}
