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

#include <cstring>
#include <string>
#include <vector>

using namespace ese::tests;

EseIntegrationScenario(LongValue, ShortLongTextRoundTrip)
{
    TemporaryDirectory directory("LongValue.ShortLongTextRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Documents");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongText, 0, 0, 1252);

    static const std::string WrittenValue = "Hello, ESE long value.";

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                      bodyColumnId,
                                      WrittenValue.data(),
                                      static_cast<uint32_t>(WrittenValue.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBytes =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId, 64);
    Require(readBytes.size() == WrittenValue.size());
    Require(std::memcmp(readBytes.data(),
                        WrittenValue.data(),
                        WrittenValue.size()) == 0);
}

EseIntegrationScenario(LongValue, MultiPageLongValueRoundTrip)
{
    TemporaryDirectory directory("LongValue.MultiPageLongValueRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "MultiPage");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // Default page size is 4 K; write 64 K so the LV definitively
    // spills across multiple internal pages.
    static constexpr int LongValueBytes = 64 * 1024;
    std::vector<uint8_t> writtenBytes(LongValueBytes);
    for (int byteIndex = 0; byteIndex < LongValueBytes; ++byteIndex)
    {
        writtenBytes[byteIndex] = static_cast<uint8_t>((byteIndex * 23) & 0xFF);
    }

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                      bodyColumnId,
                                      writtenBytes.data(),
                                      static_cast<uint32_t>(writtenBytes.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBytes =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId, LongValueBytes);
    Require(readBytes.size() == writtenBytes.size());
    Require(std::memcmp(readBytes.data(),
                        writtenBytes.data(),
                        writtenBytes.size()) == 0);
}

EseIntegrationScenario(LongValue, AppendExtendsExistingValue)
{
    TemporaryDirectory directory("LongValue.AppendExtendsExistingValue");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Append");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static const std::vector<uint8_t> FirstChunk(2'048, 0xAA);
    static const std::vector<uint8_t> SecondChunk(2'048, 0xBB);

    // Insert with the first chunk, then append the second using
    // JET_bitSetAppendLV.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              bodyColumnId,
                              FirstChunk.data(),
                              static_cast<uint32_t>(FirstChunk.size()),
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepReplace));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              bodyColumnId,
                              SecondChunk.data(),
                              static_cast<uint32_t>(SecondChunk.size()),
                              JET_bitSetAppendLV,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto combined = RetrieveVariableColumnFromCurrentRecord(table,
                                                            bodyColumnId,
                                                            8'192);
    Require(combined.size() == FirstChunk.size() + SecondChunk.size());
    Require(std::memcmp(combined.data(),
                        FirstChunk.data(),
                        FirstChunk.size()) == 0);
    Require(std::memcmp(combined.data() + FirstChunk.size(),
                        SecondChunk.data(),
                        SecondChunk.size()) == 0);
}

// TODO Phase 5: JET_bitSetOverwriteLV requires the LV to be stored as
// discrete on-disk chunks; the engine inlines small LVs (< some
// threshold), so a 4 KB-of-zeros insert isn't actually chunked and
// JetSetColumn(...SetOverwriteLV...) returns JET_errColumnNoChunk.
// Bring this back once we understand the threshold / can force the
// engine onto the chunked path.

EseIntegrationScenario(LongValue, ChunkedRetrieveReadsByOffset)
{
    TemporaryDirectory directory("LongValue.ChunkedRetrieveReadsByOffset");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Chunked");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static constexpr int LongValueBytes = 16'384;
    std::vector<uint8_t> writtenBytes(LongValueBytes);
    for (int byteIndex = 0; byteIndex < LongValueBytes; ++byteIndex)
    {
        writtenBytes[byteIndex] = static_cast<uint8_t>(byteIndex & 0xFF);
    }

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                      bodyColumnId,
                                      writtenBytes.data(),
                                      static_cast<uint32_t>(writtenBytes.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // Walk the LV in 4 KB windows.
    static constexpr uint32_t WindowBytes = 4'096;
    std::vector<uint8_t> windowBuffer(WindowBytes);
    std::vector<uint8_t> reassembled;
    reassembled.reserve(LongValueBytes);

    for (uint32_t offset = 0; offset < LongValueBytes; offset += WindowBytes)
    {
        JET_RETINFO retrieveInformation = {};
        retrieveInformation.cbStruct = sizeof(retrieveInformation);
        retrieveInformation.ibLongValue = offset;
        retrieveInformation.itagSequence = 1;

        uint32_t actualBytes = 0;
        const auto retrieveResult = JetRetrieveColumn(session.Handle(),
                                                      table.Id(),
                                                      bodyColumnId,
                                                      windowBuffer.data(),
                                                      WindowBytes,
                                                      &actualBytes,
                                                      0,
                                                      &retrieveInformation);
        // pcbActual reports the remaining LV size from `offset`; the
        // engine copies min(WindowBytes, remaining) into the buffer.
        // A short final read returns JET_wrnBufferTruncated when the
        // remaining-from-offset still exceeds the buffer.
        const uint32_t remaining = LongValueBytes - offset;
        const uint32_t copied = remaining > WindowBytes ? WindowBytes : remaining;
        Require(retrieveResult == JET_errSuccess ||
                retrieveResult == JET_wrnBufferTruncated);
        Require(actualBytes == remaining);
        reassembled.insert(reassembled.end(),
                           windowBuffer.begin(),
                           windowBuffer.begin() + copied);
    }

    Require(reassembled.size() == writtenBytes.size());
    Require(std::memcmp(reassembled.data(),
                        writtenBytes.data(),
                        writtenBytes.size()) == 0);
}

EseIntegrationScenario(LongValue, RetrieveIntoTooSmallBufferReturnsBufferTruncated)
{
    TemporaryDirectory directory(
        "LongValue.RetrieveIntoTooSmallBufferReturnsBufferTruncated");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Truncated");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static const std::vector<uint8_t> WrittenValue(1'024, 0x42);

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                      bodyColumnId,
                                      WrittenValue.data(),
                                      static_cast<uint32_t>(WrittenValue.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    uint8_t shortBuffer[16] = {};
    uint32_t actualBytes = 0;
    const auto retrieveResult = JetRetrieveColumn(session.Handle(),
                                                  table.Id(),
                                                  bodyColumnId,
                                                  shortBuffer,
                                                  sizeof(shortBuffer),
                                                  &actualBytes,
                                                  0,
                                                  nullptr);
    Require(retrieveResult == JET_wrnBufferTruncated);
    Require(actualBytes == WrittenValue.size());
}

EseIntegrationScenario(LongValue, EmptyLongValueRoundTrip)
{
    TemporaryDirectory directory("LongValue.EmptyLongValueRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Empty");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // Insert a row with the LV explicitly set to zero bytes (distinct
    // from "not set" — the column is NOT NULL via an explicit set).
    static const uint8_t EmptyMarker[1] = { 0 };
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              bodyColumnId,
                              EmptyMarker,
                              0, // zero-byte payload
                              JET_bitSetZeroLength,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    uint8_t scratch = 0;
    uint32_t actualBytes = 0;
    const auto retrieveResult = JetRetrieveColumn(session.Handle(),
                                                  table.Id(),
                                                  bodyColumnId,
                                                  &scratch,
                                                  sizeof(scratch),
                                                  &actualBytes,
                                                  0,
                                                  nullptr);
    Require(retrieveResult == JET_errSuccess);
    Require(actualBytes == 0);
}

EseIntegrationScenario(LongValue, ReplaceLongValueShrinksToZero)
{
    TemporaryDirectory directory("LongValue.ReplaceLongValueShrinksToZero");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Shrink");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static const std::vector<uint8_t> LargeValue(8'192, 0xAA);
    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                      bodyColumnId,
                                      LargeValue.data(),
                                      static_cast<uint32_t>(LargeValue.size()));
        transaction.Commit();
    }

    // Replace with a smaller value.
    static const std::vector<uint8_t> SmallValue(16, 0xBB);
    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepReplace));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              bodyColumnId,
                              SmallValue.data(),
                              static_cast<uint32_t>(SmallValue.size()),
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto observed = RetrieveVariableColumnFromCurrentRecord(table,
                                                            bodyColumnId,
                                                            LargeValue.size());
    Require(observed.size() == SmallValue.size());
    Require(std::memcmp(observed.data(),
                        SmallValue.data(),
                        SmallValue.size()) == 0);
}
