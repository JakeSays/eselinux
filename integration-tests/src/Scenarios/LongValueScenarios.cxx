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
#include <string_view>
#include <vector>

using namespace ese::tests;

EseIntegrationScenario(LongValue, ShortLongTextRoundTrip, Smoke)
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

EseIntegrationScenario(LongValue, MultiPageLongValueRoundTrip, Smoke)
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

EseIntegrationScenario(LongValue, AppendExtendsExistingValue, Smoke)
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

EseIntegrationScenario(LongValue, OverwritePortionPreservesSurroundingBytes, Smoke)
{
    TemporaryDirectory directory(
        "LongValue.OverwritePortionPreservesSurroundingBytes");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Overwrite");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static constexpr int OriginalBytes = 4'096;
    std::vector<uint8_t> writtenBytes(OriginalBytes, 0x00);

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table, bodyColumnId,
                                      writtenBytes.data(),
                                      static_cast<uint32_t>(writtenBytes.size()));
        transaction.Commit();
    }

    // Overwrite a 256-byte window at offset 1024 with 0xFF bytes.  The
    // engine routes JetSetColumn through ErrLVOpFromGrbit; that helper
    // gates JET_bitSetOverwriteLV on `fNewInstance`, which it derives
    // from `0 == itagSequence` (lv.cxx around line 2501).  Leaving
    // JET_SETINFO.itagSequence at its default 0 means the engine
    // thinks the LV is being created fresh and refuses the overwrite
    // with JET_errColumnNoChunk.  itagSequence = 1 says "modify the
    // existing LV instance" — that's the path that actually overwrites.
    static constexpr uint32_t OverwriteOffset = 1024;
    static constexpr uint32_t OverwriteBytes = 256;
    const std::vector<uint8_t> patch(OverwriteBytes, 0xFF);

    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepReplace));

        JET_SETINFO setInformation = {};
        setInformation.cbStruct = sizeof(setInformation);
        setInformation.ibLongValue = OverwriteOffset;
        setInformation.itagSequence = 1;

        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              patch.data(), OverwriteBytes,
                              JET_bitSetOverwriteLV, &setInformation));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto combined = RetrieveVariableColumnFromCurrentRecord(table,
                                                            bodyColumnId,
                                                            OriginalBytes);
    Require(combined.size() == OriginalBytes);
    for (uint32_t byteIndex = 0; byteIndex < OriginalBytes; ++byteIndex)
    {
        const bool isInPatch = byteIndex >= OverwriteOffset &&
                               byteIndex < OverwriteOffset + OverwriteBytes;
        Require(combined[byteIndex] == (isInPatch ? 0xFF : 0x00));
    }
}

EseIntegrationScenario(LongValue, ChunkedRetrieveReadsByOffset, Smoke)
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

EseIntegrationScenario(LongValue, RetrieveIntoTooSmallBufferReturnsBufferTruncated, Smoke)
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

EseIntegrationScenario(LongValue, EmptyLongValueRoundTrip, Smoke)
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

EseIntegrationScenario(LongValue, SetSeparateLVForcesSeparatedStorage, Smoke)
{
    TemporaryDirectory directory("LongValue.SetSeparateLVForcesSeparatedStorage");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Separation");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);
    auto tagColumnId = table.AddColumn("Tag", JET_coltypLong, JET_bitColumnNotNULL);

    // A short LV stays intrinsic to the record by default — small
    // enough that the engine has no reason to spill it.  Forcing
    // JET_bitSetSeparateLV must move the same-size payload into the
    // long-value tree.  JetGetRecordSize2 reports the on-disk byte
    // count attributed to the LV tree (cbLongValueData) so we can
    // distinguish "inline" (== 0) from "separated" (== payload).
    static constexpr uint32_t ShortPayloadBytes = 256;
    std::vector<uint8_t> shortPayload(ShortPayloadBytes, 0xA5);

    {
        EseTransaction transaction(session);
        // Row 1: default flags — small payload, expected to stay
        // inline in the record.
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t inlineTag = 1;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), tagColumnId,
                              &inlineTag, sizeof(inlineTag), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              shortPayload.data(),
                              static_cast<uint32_t>(shortPayload.size()),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));

        // Row 2: same payload, but force separation.
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t separatedTag = 2;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), tagColumnId,
                              &separatedTag, sizeof(separatedTag), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              shortPayload.data(),
                              static_cast<uint32_t>(shortPayload.size()),
                              JET_bitSetSeparateLV, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    auto reportLongValueBytes = [&]() -> uint64_t
    {
        JET_RECSIZE2 recordSize = {};
        CheckJet(JetGetRecordSize2(session.Handle(), table.Id(),
                                   &recordSize, 0));
        return recordSize.cbLongValueData;
    };

    // Row 1: inline.  No LV-tree bytes attributed to this record.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, tagColumnId)
            == 1);
    Require(reportLongValueBytes() == 0);
    const auto inlineBytes =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                ShortPayloadBytes);
    Require(inlineBytes.size() == ShortPayloadBytes);
    Require(std::memcmp(inlineBytes.data(), shortPayload.data(),
                        ShortPayloadBytes) == 0);

    // Row 2: separated.  The LV tree holds the payload.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, tagColumnId)
            == 2);
    Require(reportLongValueBytes() == ShortPayloadBytes);
    const auto separatedBytes =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                ShortPayloadBytes);
    Require(separatedBytes.size() == ShortPayloadBytes);
    Require(std::memcmp(separatedBytes.data(), shortPayload.data(),
                        ShortPayloadBytes) == 0);
}

EseIntegrationScenario(LongValue, SetCompressedShrinksHighlyCompressibleData, Smoke)
{
    TemporaryDirectory directory(
        "LongValue.SetCompressedShrinksHighlyCompressibleData");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Compressed");

    // Column is NOT compressed by default; per-set JET_bitSetCompressed
    // forces compression for this particular value.
    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // 32 KiB of a single byte compresses to almost nothing under the
    // engine's xpress/RLE backends.  Keep the payload size small enough
    // that the LV tree's overhead doesn't dwarf the compressed payload.
    static constexpr uint32_t PayloadBytes = 32 * 1024;
    const std::vector<uint8_t> compressiblePayload(PayloadBytes, 0xAA);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              compressiblePayload.data(),
                              static_cast<uint32_t>(compressiblePayload.size()),
                              JET_bitSetCompressed, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // JET_bitRetrievePhysicalSize asks the engine to report the
    // on-disk byte count in pcbActual; no data is copied into the
    // buffer.  For a column that successfully compressed, this is
    // strictly less than the logical PayloadBytes.
    uint32_t physicalBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), bodyColumnId,
                               nullptr, 0, &physicalBytes,
                               JET_bitRetrievePhysicalSize, nullptr));
    Require(physicalBytes > 0);
    Require(physicalBytes < PayloadBytes);

    // The decompressed read still matches the original bytes.
    const auto roundTrip =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                PayloadBytes);
    Require(roundTrip.size() == PayloadBytes);
    Require(std::memcmp(roundTrip.data(), compressiblePayload.data(),
                        PayloadBytes) == 0);
}

EseIntegrationScenario(LongValue, SetUncompressedOverridesColumnDefaultCompression, Smoke)
{
    TemporaryDirectory directory(
        "LongValue.SetUncompressedOverridesColumnDefaultCompression");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Mixed");

    // Column is created with JET_bitColumnCompressed, so the engine
    // compresses by default.  We then insert two rows: one taking the
    // default (compressed), one passing JET_bitSetUncompressed to
    // bypass compression for that specific value.
    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary,
                                        JET_bitColumnCompressed);
    auto tagColumnId = table.AddColumn("Tag", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr uint32_t PayloadBytes = 32 * 1024;
    const std::vector<uint8_t> compressiblePayload(PayloadBytes, 0x42);

    {
        EseTransaction transaction(session);
        // Row 1: default (compressed by column flag).
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t compressedTag = 1;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), tagColumnId,
                              &compressedTag, sizeof(compressedTag), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              compressiblePayload.data(),
                              static_cast<uint32_t>(compressiblePayload.size()),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));

        // Row 2: same payload, but the SetUncompressed flag wins over
        // the column default.
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t uncompressedTag = 2;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), tagColumnId,
                              &uncompressedTag, sizeof(uncompressedTag),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              compressiblePayload.data(),
                              static_cast<uint32_t>(compressiblePayload.size()),
                              JET_bitSetUncompressed, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    auto readPhysicalBytes = [&]() -> uint32_t
    {
        uint32_t physicalBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), bodyColumnId,
                                   nullptr, 0, &physicalBytes,
                                   JET_bitRetrievePhysicalSize, nullptr));
        return physicalBytes;
    };

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, tagColumnId)
            == 1);
    const uint32_t compressedPhysical = readPhysicalBytes();
    Require(compressedPhysical < PayloadBytes);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, tagColumnId)
            == 2);
    const uint32_t uncompressedPhysical = readPhysicalBytes();
    // Uncompressed on-disk size equals payload (no compression
    // overhead beyond what the column header always carries).
    Require(uncompressedPhysical == PayloadBytes);

    // Both rows decompress back to the same bytes.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    const auto compressedRoundTrip =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                PayloadBytes);
    Require(compressedRoundTrip.size() == PayloadBytes);
    Require(std::memcmp(compressedRoundTrip.data(),
                        compressiblePayload.data(), PayloadBytes) == 0);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
    const auto uncompressedRoundTrip =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                PayloadBytes);
    Require(uncompressedRoundTrip.size() == PayloadBytes);
    Require(std::memcmp(uncompressedRoundTrip.data(),
                        compressiblePayload.data(), PayloadBytes) == 0);
}

EseIntegrationScenario(LongValue, SetSizeLVTruncatesPreservingPrefix, Smoke)
{
    TemporaryDirectory directory("LongValue.SetSizeLVTruncatesPreservingPrefix");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Truncate");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static constexpr uint32_t OriginalBytes = 8 * 1024;
    std::vector<uint8_t> originalPayload(OriginalBytes);
    for (uint32_t byteIndex = 0; byteIndex < OriginalBytes; ++byteIndex)
    {
        originalPayload[byteIndex] = static_cast<uint8_t>(byteIndex & 0xFF);
    }

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table, bodyColumnId,
                                      originalPayload.data(),
                                      static_cast<uint32_t>(originalPayload.size()));
        transaction.Commit();
    }

    // Use JET_bitSetSizeLV to truncate the LV to TruncatedBytes WITHOUT
    // supplying new payload bytes — the engine should keep the first
    // TruncatedBytes of the original.  cbData=TruncatedBytes encodes
    // the target size; the pvData buffer is ignored.
    static constexpr uint32_t TruncatedBytes = 2 * 1024;
    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepReplace));

        JET_SETINFO setInformation = {};
        setInformation.cbStruct = sizeof(setInformation);
        setInformation.itagSequence = 1;

        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              originalPayload.data(), TruncatedBytes,
                              JET_bitSetSizeLV, &setInformation));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    const auto truncated =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                OriginalBytes);
    Require(truncated.size() == TruncatedBytes);
    Require(std::memcmp(truncated.data(), originalPayload.data(),
                        TruncatedBytes) == 0);
}

EseIntegrationScenario(LongValue, RetrieveLongIdReturnsEightByteHandle, Smoke)
{
    TemporaryDirectory directory("LongValue.RetrieveLongIdReturnsEightByteHandle");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "LongIds");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // Force the LV onto the long-value tree so the engine actually
    // has an LID to return — JET_bitRetrieveLongId on an intrinsic
    // (inline) value comes back as JET_wrnColumnNotInRecord.
    static constexpr uint32_t PayloadBytes = 4 * 1024;
    const std::vector<uint8_t> payload(PayloadBytes, 0x7E);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              payload.data(),
                              static_cast<uint32_t>(payload.size()),
                              JET_bitSetSeparateLV, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // The LID is an opaque 8-byte handle pointing to the LV root.
    static constexpr uint32_t LongIdBytes = 8;
    uint8_t longIdBuffer[LongIdBytes] = {};
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), bodyColumnId,
                               longIdBuffer, sizeof(longIdBuffer),
                               &actualBytes,
                               JET_bitRetrieveLongId, nullptr));
    Require(actualBytes == LongIdBytes);

    // The LID must be non-zero — a zero handle would mean "no LV".
    bool anyNonZero = false;
    for (auto byte : longIdBuffer)
    {
        if (byte != 0)
        {
            anyNonZero = true;
        }
    }
    Require(anyNonZero);

    // Normal retrieve still returns the payload bytes.
    const auto roundTrip =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId, PayloadBytes);
    Require(roundTrip.size() == PayloadBytes);
    Require(std::memcmp(roundTrip.data(), payload.data(), PayloadBytes) == 0);
}

EseIntegrationScenario(LongValue, SetIntrinsicLVForcesInlineStorage, Smoke)
{
    TemporaryDirectory directory("LongValue.SetIntrinsicLVForcesInlineStorage");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Intrinsic");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // A small payload with JET_bitSetIntrinsicLV must stay inline in
    // the record.  JET_RECSIZE2.cbLongValueData reports 0 (no LV-tree
    // footprint) confirming the LV did NOT separate.
    static constexpr uint32_t IntrinsicPayloadBytes = 128;
    const std::vector<uint8_t> intrinsicPayload(IntrinsicPayloadBytes, 0x33);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              intrinsicPayload.data(),
                              static_cast<uint32_t>(intrinsicPayload.size()),
                              JET_bitSetIntrinsicLV, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    JET_RECSIZE2 recordSize = {};
    CheckJet(JetGetRecordSize2(session.Handle(), table.Id(),
                               &recordSize, 0));
    Require(recordSize.cbLongValueData == 0);

    const auto roundTrip =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                IntrinsicPayloadBytes);
    Require(roundTrip.size() == IntrinsicPayloadBytes);
    Require(std::memcmp(roundTrip.data(), intrinsicPayload.data(),
                        IntrinsicPayloadBytes) == 0);
}

EseIntegrationScenario(LongValue, SetIntrinsicLVSilentlySeparatesOversizeValue, Smoke)
{
    TemporaryDirectory directory(
        "LongValue.SetIntrinsicLVSilentlySeparatesOversizeValue");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Oversize");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // The jetapi.h comment for JET_bitSetIntrinsicLV reads "store whole
    // LV in record without bursting or return an error" — but the
    // actual engine path (lv.cxx:2762) silently sets fForceSeparateLV
    // = TRUE when cbIntrinsicPhysical > cbPreferredIntrinsicLV and
    // falls back to a separated LV.  The header comment lies about the
    // "or return an error" half; this scenario pins the real behavior
    // so a future tightening of the engine to match the docs surfaces
    // as a deliberate test update, not a silent semantics change.
    static constexpr uint32_t OversizePayloadBytes = 64 * 1024;
    std::vector<uint8_t> oversizePayload(OversizePayloadBytes);
    for (uint32_t index = 0; index < OversizePayloadBytes; ++index)
    {
        oversizePayload[index] = static_cast<uint8_t>(index & 0xFF);
    }

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              oversizePayload.data(),
                              static_cast<uint32_t>(oversizePayload.size()),
                              JET_bitSetIntrinsicLV, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // The engine accepted the oversize LV and stored it in the long-
    // value tree — JET_RECSIZE2.cbLongValueData attributes the bytes
    // to the LV tree, not the record body.
    JET_RECSIZE2 recordSize = {};
    CheckJet(JetGetRecordSize2(session.Handle(), table.Id(),
                               &recordSize, 0));
    Require(recordSize.cbLongValueData > 0);

    // The full payload still round-trips through a normal retrieve.
    const auto roundTrip =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                OversizePayloadBytes);
    Require(roundTrip.size() == OversizePayloadBytes);
    Require(std::memcmp(roundTrip.data(), oversizePayload.data(),
                        OversizePayloadBytes) == 0);
}

EseIntegrationScenario(LongValue, SetContiguousLVRoundTripsLargeData, Smoke)
{
    TemporaryDirectory directory("LongValue.SetContiguousLVRoundTripsLargeData");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LongValue.mdb");
    EseTable table(database, "Contiguous");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // JET_bitSetContiguousLV asks the engine to allocate the LV across
    // contiguous pages for better I/O behavior — useful for sequential
    // scans of large values.  Must be paired with JET_bitSetSeparateLV
    // per the jetapi.h flag comment (combinations with replace and
    // certain column options are documented as invalid).  No public
    // API observes "actually contiguous on disk"; this scenario pins
    // correctness — the flag is accepted and the payload round-trips.
    static constexpr uint32_t PayloadBytes = 256 * 1024;
    std::vector<uint8_t> payload(PayloadBytes);
    for (uint32_t index = 0; index < PayloadBytes; ++index)
    {
        payload[index] = static_cast<uint8_t>((index * 31) & 0xFF);
    }

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                              payload.data(),
                              static_cast<uint32_t>(payload.size()),
                              JET_bitSetSeparateLV | JET_bitSetContiguousLV,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // The LV must live in the long-value tree (separated), and the
    // payload must round-trip byte-exactly.
    JET_RECSIZE2 recordSize = {};
    CheckJet(JetGetRecordSize2(session.Handle(), table.Id(),
                               &recordSize, 0));
    Require(recordSize.cbLongValueData >= PayloadBytes);

    const auto roundTrip =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                PayloadBytes);
    Require(roundTrip.size() == PayloadBytes);
    Require(std::memcmp(roundTrip.data(), payload.data(),
                        PayloadBytes) == 0);
}

EseIntegrationScenario(LongValue, ReplaceLongValueShrinksToZero, Smoke)
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

//  ===================================================================
//  Tier::Regression — long-value boundary + size diversity.
//
//  Smoke tests cover single LVs at one size each.  This scenario
//  drives the LV separation-threshold decision with many sizes
//  bracketing the boundary (a few bytes below to many KB above),
//  then verifies every payload round-trips byte-for-byte AFTER
//  detach + reattach.  A refactor that broke the
//  intrinsic-vs-separated decision at a specific size threshold,
//  or that mis-serialised the LV root pointer, would surface here.
//  ===================================================================
EseIntegrationScenario(LongValue, MixedSizeLongValuesAcrossSeparationBoundary, Regression)
{
    TemporaryDirectory directory(
        "LongValue.MixedSizeLongValuesAcrossSeparationBoundary");
    const auto databasePath = directory.Path() / "Boundary.mdb";

    //  Sizes chosen to span the intrinsic/separated boundary (the
    //  engine separates above ~half-page, typically ~1–2 KiB) and
    //  to push deep into multi-page LVs (>16 KiB).  Each row's
    //  payload is a deterministic pattern (byte_i = i * 31 + key
    //  XOR'd through) so a corrupted byte at any offset shows up.
    const std::vector<uint32_t> Sizes = {
        16, 256, 1024, 1536, 2048, 4096, 5000, 8192, 12000, 24576,
    };
    static constexpr int32_t RowCount = static_cast<int32_t>(10);

    auto makePayload = [](int32_t key, uint32_t size) {
        std::vector<uint8_t> bytes(size);
        for (uint32_t i = 0; i < size; ++i)
        {
            bytes[i] = static_cast<uint8_t>(
                ((i * 31) ^ (key * 7) ^ (i >> 3)) & 0xFF);
        }
        return bytes;
    };

    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Boundary.mdb");
        EseTable table(database, "LongValues");
        auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                         JET_bitColumnNotNULL);
        auto bodyColumn = table.AddColumn("Body", JET_coltypLongBinary);
        static constexpr std::string_view PrimaryKey =
            std::string_view("+Key\0\0", 6);
        table.CreateIndex("PrimaryByKey", PrimaryKey,
                          JET_bitIndexPrimary | JET_bitIndexUnique);

        EseTransaction transaction(session);
        for (int32_t key = 0; key < RowCount; ++key)
        {
            const auto payload = makePayload(key, Sizes[key]);
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  keyColumn, &key, sizeof(key),
                                  0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  bodyColumn,
                                  payload.data(), payload.size(),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
        //  Scope-exit RAII closes table + database, detaches, and
        //  JetTerms the instance — no manual JetDetachDatabaseA
        //  needed (and a manual detach here would race the
        //  destructors).
    }

    //  Reattach in a fresh instance — verifies the LV root
    //  pointers + multi-page LV nodes survive the detach/attach
    //  cycle, not just the buffer cache.
    EseInstance instance(directory);
    EseSession session(instance);
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.string().c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), dbid, "LongValues",
                           nullptr, 0, 0, &tableId));
    JET_COLUMNDEF keyInfo = {};
    keyInfo.cbStruct = sizeof(keyInfo);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId, "Key",
                                     &keyInfo, sizeof(keyInfo),
                                     JET_ColInfo));
    JET_COLUMNDEF bodyInfo = {};
    bodyInfo.cbStruct = sizeof(bodyInfo);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId, "Body",
                                     &bodyInfo, sizeof(bodyInfo),
                                     JET_ColInfo));

    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    int32_t walkedRowCount = 0;
    while (true)
    {
        int32_t observedKey = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                   keyInfo.columnid,
                                   &observedKey, sizeof(observedKey),
                                   &actualBytes, 0, nullptr));
        const auto expectedSize = Sizes[observedKey];
        std::vector<uint8_t> observedBody(expectedSize);
        CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                   bodyInfo.columnid,
                                   observedBody.data(), expectedSize,
                                   &actualBytes, 0, nullptr));
        Require(actualBytes == expectedSize);
        const auto expected = makePayload(observedKey, expectedSize);
        Require(std::memcmp(observedBody.data(),
                            expected.data(),
                            expectedSize) == 0);
        ++walkedRowCount;
        const auto moveResult = JetMove(session.Handle(), tableId,
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walkedRowCount == RowCount);

    CheckJet(JetCloseTable(session.Handle(), tableId));
    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                databasePath.string().c_str()));
}

//  ===================================================================
//  Tier::Regression — LV append builds correctly across many
//  chunks that span the separation threshold.  Smoke append test
//  uses 2 chunks; this appends 8 chunks of varying sizes so the
//  cumulative total crosses the separation boundary mid-append
//  and the engine has to migrate the LV root pointer in place.
//  ===================================================================
EseIntegrationScenario(LongValue, AppendAcrossManyChunksSpansSeparationBoundary, Regression)
{
    TemporaryDirectory directory(
        "LongValue.AppendAcrossManyChunksSpansSeparationBoundary");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "AppendMany.mdb");
    EseTable table(database, "Rows");
    auto bodyColumn = table.AddColumn("Body", JET_coltypLongBinary);

    //  Eight chunks, cumulative sizes:
    //  256, 512, 1280, 2304, 3328, 5376, 7424, 11520
    //  Crosses the typical ~1.5 KiB separation threshold between
    //  chunks 3 and 4.
    const std::vector<uint32_t> ChunkSizes = {
        256, 256, 768, 1024, 1024, 2048, 2048, 4096,
    };

    auto patternByte = [](uint32_t offset) {
        return static_cast<uint8_t>((offset * 17 + (offset >> 4)) & 0xFF);
    };

    //  First chunk inserts; subsequent chunks must use JET_prepReplace
    //  + JET_bitSetAppendLV with NO setInfo (engine appends to the
    //  current LV).
    uint32_t totalSoFar = 0;
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        std::vector<uint8_t> chunk(ChunkSizes[0]);
        for (uint32_t i = 0; i < ChunkSizes[0]; ++i)
        {
            chunk[i] = patternByte(i);
        }
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              chunk.data(), chunk.size(),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
        totalSoFar = ChunkSizes[0];
    }
    for (size_t ci = 1; ci < ChunkSizes.size(); ++ci)
    {
        const uint32_t cb = ChunkSizes[ci];
        std::vector<uint8_t> chunk(cb);
        for (uint32_t i = 0; i < cb; ++i)
        {
            chunk[i] = patternByte(totalSoFar + i);
        }
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(),
                         JET_MoveFirst, 0));
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepReplace));
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              chunk.data(), chunk.size(),
                              JET_bitSetAppendLV, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
        totalSoFar += cb;
    }

    uint32_t totalExpected = 0;
    for (uint32_t cb : ChunkSizes)
    {
        totalExpected += cb;
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    std::vector<uint8_t> readBack(totalExpected);
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                               bodyColumn,
                               readBack.data(), readBack.size(),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == totalExpected);
    for (uint32_t i = 0; i < totalExpected; ++i)
    {
        Require(readBack[i] == patternByte(i));
    }
}

//  ===================================================================
//  Tier::Regression — overwrite at LV boundary offsets.  Smoke
//  test overwrites the middle of a 4 KiB LV.  This overwrites
//  the FIRST byte, the LAST byte, and a slab exactly straddling
//  a 4 KiB page boundary on a separated LV.
//  ===================================================================
EseIntegrationScenario(LongValue, OverwriteAtPageBoundariesPreservesEverythingElse, Regression)
{
    TemporaryDirectory directory(
        "LongValue.OverwriteAtPageBoundariesPreservesEverythingElse");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Overwrite.mdb");
    EseTable table(database, "Rows");
    auto bodyColumn = table.AddColumn("Body", JET_coltypLongBinary);

    //  16 KiB body — multi-page separated LV.  Initial pattern:
    //  byte_i = i & 0xFF.
    static constexpr uint32_t TotalSize = 16 * 1024;
    std::vector<uint8_t> body(TotalSize);
    for (uint32_t i = 0; i < TotalSize; ++i)
    {
        body[i] = static_cast<uint8_t>(i & 0xFF);
    }

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              body.data(), body.size(),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    //  Overwrite at offset 0 (the start byte).
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepReplace));
        const uint8_t firstByteOverride = 0xAA;
        JET_SETINFO setInfo = {};
        setInfo.cbStruct = sizeof(setInfo);
        setInfo.itagSequence = 1;
        setInfo.ibLongValue = 0;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              &firstByteOverride,
                              sizeof(firstByteOverride),
                              JET_bitSetOverwriteLV, &setInfo));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
        body[0] = firstByteOverride;
    }
    //  Overwrite at offset TotalSize-1 (last byte).
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepReplace));
        const uint8_t lastByteOverride = 0x55;
        JET_SETINFO setInfo = {};
        setInfo.cbStruct = sizeof(setInfo);
        setInfo.itagSequence = 1;
        setInfo.ibLongValue = TotalSize - 1;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              &lastByteOverride,
                              sizeof(lastByteOverride),
                              JET_bitSetOverwriteLV, &setInfo));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
        body[TotalSize - 1] = lastByteOverride;
    }
    //  Overwrite a 256-byte slab straddling the 4096-byte
    //  boundary at offset 3968 (extends 3968..4223).  Page
    //  boundary is at byte 4096; the write spans two LV pages.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepReplace));
        std::array<uint8_t, 256> slab{};
        for (uint32_t i = 0; i < slab.size(); ++i)
        {
            slab[i] = static_cast<uint8_t>(0xC0 + (i & 0x1F));
        }
        JET_SETINFO setInfo = {};
        setInfo.cbStruct = sizeof(setInfo);
        setInfo.itagSequence = 1;
        setInfo.ibLongValue = 3968;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              slab.data(), slab.size(),
                              JET_bitSetOverwriteLV, &setInfo));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
        std::memcpy(body.data() + 3968, slab.data(), slab.size());
    }

    //  Final readback: every byte must match the predicted
    //  pattern with the three overwrites applied.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    std::vector<uint8_t> readBack(TotalSize);
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                               bodyColumn,
                               readBack.data(), readBack.size(),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == TotalSize);
    Require(std::memcmp(readBack.data(), body.data(), TotalSize) == 0);
}

//  ===================================================================
//  Tier::Regression — replace LV with a smaller payload, verify
//  the engine returns the predicted size (not the original).
//  Catches refactors to LV tree node shrinking.
//  ===================================================================
EseIntegrationScenario(LongValue, ReplaceShrinksLVAndReportsNewSize, Regression)
{
    TemporaryDirectory directory(
        "LongValue.ReplaceShrinksLVAndReportsNewSize");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Shrink.mdb");
    EseTable table(database, "Rows");
    auto bodyColumn = table.AddColumn("Body", JET_coltypLongBinary);

    //  Original 32 KiB separated LV.
    std::vector<uint8_t> original(32 * 1024, 0xAB);
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              original.data(), original.size(),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }

    //  Replace with 16 bytes — engine must shrink the LV.
    static constexpr uint8_t Replacement[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    };
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepReplace));
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn,
                              Replacement, sizeof(Replacement),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    uint8_t readBack[64] = {};
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                               bodyColumn,
                               readBack, sizeof(readBack),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == sizeof(Replacement));
    Require(std::memcmp(readBack, Replacement,
                        sizeof(Replacement)) == 0);
}

