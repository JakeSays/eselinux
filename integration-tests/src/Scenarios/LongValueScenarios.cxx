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

EseIntegrationScenario(LongValue, OverwritePortionPreservesSurroundingBytes)
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

EseIntegrationScenario(LongValue, SetSeparateLVForcesSeparatedStorage)
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

EseIntegrationScenario(LongValue, SetCompressedShrinksHighlyCompressibleData)
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

EseIntegrationScenario(LongValue, SetUncompressedOverridesColumnDefaultCompression)
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

EseIntegrationScenario(LongValue, SetSizeLVTruncatesPreservingPrefix)
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

EseIntegrationScenario(LongValue, RetrieveLongIdReturnsEightByteHandle)
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
