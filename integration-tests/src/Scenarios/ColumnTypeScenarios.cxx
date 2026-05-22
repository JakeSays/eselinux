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

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace ese::tests;

namespace
{

// Round-trip one fixed-size value through a single-column table.
// Each scenario opens its own directory / instance / session so
// failures don't cross-contaminate.
template <typename ValueType>
void FixedColumnRoundTrip(const char* scenarioName,
                          JET_COLTYP columnType,
                          ValueType writtenValue)
{
    TemporaryDirectory directory(scenarioName);
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable table(database, "RoundTrip");

    auto columnId = table.AddColumn("Value", columnType);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<ValueType>(table, columnId, writtenValue);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBack =
        RetrieveFixedColumnFromCurrentRecord<ValueType>(table, columnId);

    // Compare as bytes so float/double NaN payloads round-trip too.
    Require(std::memcmp(&readBack, &writtenValue, sizeof(writtenValue)) == 0);
}

} // namespace

EseIntegrationScenario(ColumnType, BitRoundTrip, Smoke)
{
    // JET_coltypBit canonicalises non-zero inputs to 0xFF and zero
    // inputs to 0x00 (fldmod.cxx around line 2535). Test both the
    // canonical "true" and canonical "false" round-trips explicitly.
    FixedColumnRoundTrip<uint8_t>("ColumnType.BitRoundTrip",
                                  JET_coltypBit,
                                  static_cast<uint8_t>(0xFF));
}

EseIntegrationScenario(ColumnType, BitZeroRoundTrip, Smoke)
{
    FixedColumnRoundTrip<uint8_t>("ColumnType.BitZeroRoundTrip",
                                  JET_coltypBit,
                                  static_cast<uint8_t>(0));
}

EseIntegrationScenario(ColumnType, UnsignedByteRoundTrip, Smoke)
{
    FixedColumnRoundTrip<uint8_t>("ColumnType.UnsignedByteRoundTrip",
                                  JET_coltypUnsignedByte,
                                  static_cast<uint8_t>(0xA5));
}

EseIntegrationScenario(ColumnType, ShortRoundTrip, Smoke)
{
    FixedColumnRoundTrip<int16_t>("ColumnType.ShortRoundTrip",
                                  JET_coltypShort,
                                  static_cast<int16_t>(-12345));
}

EseIntegrationScenario(ColumnType, UnsignedShortRoundTrip, Smoke)
{
    FixedColumnRoundTrip<uint16_t>("ColumnType.UnsignedShortRoundTrip",
                                   JET_coltypUnsignedShort,
                                   static_cast<uint16_t>(0xDEAD));
}

EseIntegrationScenario(ColumnType, LongRoundTrip, Smoke)
{
    FixedColumnRoundTrip<int32_t>("ColumnType.LongRoundTrip",
                                  JET_coltypLong,
                                  static_cast<int32_t>(-2'000'000'000));
}

EseIntegrationScenario(ColumnType, UnsignedLongRoundTrip, Smoke)
{
    FixedColumnRoundTrip<uint32_t>("ColumnType.UnsignedLongRoundTrip",
                                   JET_coltypUnsignedLong,
                                   static_cast<uint32_t>(0xCAFEBABEu));
}

EseIntegrationScenario(ColumnType, CurrencyRoundTrip, Smoke)
{
    // Currency is the engine's signed 8-byte fixed-point money type.
    FixedColumnRoundTrip<int64_t>("ColumnType.CurrencyRoundTrip",
                                  JET_coltypCurrency,
                                  static_cast<int64_t>(-1'234'567'890'123LL));
}

EseIntegrationScenario(ColumnType, LongLongRoundTrip, Smoke)
{
    FixedColumnRoundTrip<int64_t>("ColumnType.LongLongRoundTrip",
                                  JET_coltypLongLong,
                                  static_cast<int64_t>(0x0123456789ABCDEFLL));
}

EseIntegrationScenario(ColumnType, UnsignedLongLongRoundTrip, Smoke)
{
    FixedColumnRoundTrip<uint64_t>("ColumnType.UnsignedLongLongRoundTrip",
                                   JET_coltypUnsignedLongLong,
                                   static_cast<uint64_t>(0xDEADBEEF'FEEDFACEULL));
}

EseIntegrationScenario(ColumnType, SingleRoundTrip, Smoke)
{
    FixedColumnRoundTrip<float>("ColumnType.SingleRoundTrip",
                                JET_coltypIEEESingle,
                                3.1415927f);
}

EseIntegrationScenario(ColumnType, DoubleRoundTrip, Smoke)
{
    FixedColumnRoundTrip<double>("ColumnType.DoubleRoundTrip",
                                 JET_coltypIEEEDouble,
                                 2.718281828459045);
}

EseIntegrationScenario(ColumnType, DateTimeRoundTrip, Smoke)
{
    // DateTime is an 8-byte fractional-day count; round-trip the bit
    // pattern directly to confirm the storage layer doesn't mutate it.
    union DateTimeBits
    {
        double fractionalDays;
        uint8_t bytes[8];
    };
    DateTimeBits writtenValue = {};
    writtenValue.fractionalDays = 44'567.5;
    FixedColumnRoundTrip<DateTimeBits>("ColumnType.DateTimeRoundTrip",
                                       JET_coltypDateTime,
                                       writtenValue);
}

EseIntegrationScenario(ColumnType, GuidRoundTrip, Smoke)
{
    struct GuidLayout
    {
        uint8_t bytes[16];
    };
    GuidLayout writtenValue = { { 0x01, 0x23, 0x45, 0x67,
                                  0x89, 0xAB, 0xCD, 0xEF,
                                  0xFE, 0xDC, 0xBA, 0x98,
                                  0x76, 0x54, 0x32, 0x10 } };
    FixedColumnRoundTrip<GuidLayout>("ColumnType.GuidRoundTrip",
                                     JET_coltypGUID,
                                     writtenValue);
}

EseIntegrationScenario(ColumnType, BinaryRoundTrip, Smoke)
{
    TemporaryDirectory directory("ColumnType.BinaryRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable table(database, "BinaryRoundTrip");

    // JET_coltypBinary caps at 255 bytes — write 200 to stay safely under.
    auto columnId = table.AddColumn("Bytes", JET_coltypBinary, 0, 255);

    static constexpr int PayloadBytes = 200;
    std::vector<uint8_t> writtenBytes(PayloadBytes);
    for (int byteIndex = 0; byteIndex < PayloadBytes; ++byteIndex)
    {
        writtenBytes[byteIndex] = static_cast<uint8_t>((byteIndex * 31) & 0xFF);
    }

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                                  columnId,
                                                  writtenBytes.data(),
                                                  static_cast<uint32_t>(writtenBytes.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBytes =
        RetrieveVariableColumnFromCurrentRecord(table, columnId, 255);
    Require(readBytes.size() == writtenBytes.size());
    Require(std::memcmp(readBytes.data(),
                        writtenBytes.data(),
                        writtenBytes.size()) == 0);
}

EseIntegrationScenario(ColumnType, TextRoundTrip, Smoke)
{
    TemporaryDirectory directory("ColumnType.TextRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable table(database, "TextRoundTrip");

    auto columnId = table.AddColumn("Greeting", JET_coltypText, 0, 255, 1252);

    static const std::string WrittenValue = "Hello, ESE column-type round-trip.";
    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                                  columnId,
                                                  WrittenValue.data(),
                                                  static_cast<uint32_t>(WrittenValue.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBytes =
        RetrieveVariableColumnFromCurrentRecord(table, columnId, 255);
    Require(readBytes.size() == WrittenValue.size());
    Require(std::memcmp(readBytes.data(),
                        WrittenValue.data(),
                        WrittenValue.size()) == 0);
}

EseIntegrationScenario(ColumnType, LongBinaryRoundTrip, Smoke)
{
    TemporaryDirectory directory("ColumnType.LongBinaryRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable table(database, "LongBinaryRoundTrip");

    auto columnId = table.AddColumn("Bytes", JET_coltypLongBinary);

    static constexpr int PayloadBytes = 8'192;
    std::vector<uint8_t> writtenBytes(PayloadBytes);
    for (int byteIndex = 0; byteIndex < PayloadBytes; ++byteIndex)
    {
        writtenBytes[byteIndex] = static_cast<uint8_t>(((byteIndex * 17) ^ 0x55) & 0xFF);
    }

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table,
                                                  columnId,
                                                  writtenBytes.data(),
                                                  static_cast<uint32_t>(writtenBytes.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBytes =
        RetrieveVariableColumnFromCurrentRecord(table, columnId, PayloadBytes);
    Require(readBytes.size() == writtenBytes.size());
    Require(std::memcmp(readBytes.data(),
                        writtenBytes.data(),
                        writtenBytes.size()) == 0);
}

EseIntegrationScenario(ColumnType, NotNullColumnRejectsNullInsert, Smoke)
{
    TemporaryDirectory directory("ColumnType.NotNullColumnRejectsNullInsert");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable table(database, "NotNullCheck");

    table.AddColumn("Required", JET_coltypLong, JET_bitColumnNotNULL);

    EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
    // Skip setting the column and try to commit — JetUpdate must
    // reject because the column is marked NOT NULL with no default.
    RequireJetError(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr),
                    JET_errNullInvalid);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepCancel));
}

EseIntegrationScenario(ColumnType, AutoincrementProducesMonotonicValues, Smoke)
{
    TemporaryDirectory directory(
        "ColumnType.AutoincrementProducesMonotonicValues");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable table(database, "Auto");

    auto identityColumnId = table.AddColumn("Identity",
                                            JET_coltypLong,
                                            JET_bitColumnAutoincrement);

    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < 5; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
            CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    int32_t previous = 0;
    bool first = true;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    for (int rowIndex = 0; rowIndex < 5; ++rowIndex)
    {
        auto value =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                      identityColumnId);
        if (first)
        {
            first = false;
        }
        else
        {
            Require(value > previous);
        }
        previous = value;
        if (rowIndex < 4)
        {
            CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
        }
    }
}

EseIntegrationScenario(ColumnType, DefaultValueAppliedWhenColumnUnset, Smoke)
{
    TemporaryDirectory directory(
        "ColumnType.DefaultValueAppliedWhenColumnUnset");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable table(database, "Defaulted");

    const int32_t defaultValue = 17;
    auto columnId = table.AddColumnWithDefault("WithDefault",
                                               JET_coltypLong,
                                               &defaultValue,
                                               sizeof(defaultValue));

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBack =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId);
    Require(readBack == defaultValue);
}

//  ===================================================================
//  Tier::Regression — column-type packing across a wide record.
//
//  Each Smoke RoundTrip covers one type with one value.  This
//  scenario packs every primitive type into a single record with
//  a distinguishable value per column, round-trips through detach
//  + reattach, and validates each column independently.  Catches
//  refactors that broke wide-record layout, byte ordering across
//  type boundaries, or per-type-specific packing under
//  cross-column alignment.
//  ===================================================================
EseIntegrationScenario(ColumnType, AllPrimitiveTypesPackedInOneRecord, Regression)
{
    TemporaryDirectory directory(
        "ColumnType.AllPrimitiveTypesPackedInOneRecord");
    const auto databasePath = directory.Path() / "AllTypes.mdb";

    //  JET_coltypBit stores 0xFF for true / 0x00 for false (engine
    //  internal — observed bit returns 0xFF on non-zero input).
    constexpr uint8_t  WrittenBit       = 1;
    constexpr uint8_t  ExpectedBit      = 0xFF;
    constexpr uint8_t  ExpectedUByte    = 0xCD;
    constexpr int16_t  ExpectedShort    = -12345;
    constexpr int32_t  ExpectedLong     = 0x12345678;
    constexpr int64_t  ExpectedCurrency = -0x0123456789ABCDEFLL;
    constexpr float    ExpectedFloat    = 3.14159f;
    constexpr double   ExpectedDouble   = 2.7182818284590452;
    static constexpr std::string_view ExpectedText =
        "the quick brown fox jumps";
    static constexpr std::array<uint8_t, 6> ExpectedBinary =
        { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE };

    JET_COLUMNID bitCol = 0, ubyteCol = 0, shortCol = 0, longCol = 0;
    JET_COLUMNID currencyCol = 0, floatCol = 0, doubleCol = 0;
    JET_COLUMNID textCol = 0, binaryCol = 0;

    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "AllTypes.mdb");
        EseTable table(database, "Wide");
        bitCol      = table.AddColumn("Bit",      JET_coltypBit);
        ubyteCol    = table.AddColumn("UByte",    JET_coltypUnsignedByte);
        shortCol    = table.AddColumn("Short",    JET_coltypShort);
        longCol     = table.AddColumn("Long",     JET_coltypLong);
        currencyCol = table.AddColumn("Currency", JET_coltypCurrency);
        floatCol    = table.AddColumn("Float",    JET_coltypIEEESingle);
        doubleCol   = table.AddColumn("Double",   JET_coltypIEEEDouble);
        textCol     = table.AddColumn("Text",     JET_coltypLongText,
                                     0, 0, 1252);
        binaryCol   = table.AddColumn("Binary",   JET_coltypLongBinary);

        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), bitCol,
                              &WrittenBit, sizeof(WrittenBit),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), ubyteCol,
                              &ExpectedUByte, sizeof(ExpectedUByte),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), shortCol,
                              &ExpectedShort, sizeof(ExpectedShort),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), longCol,
                              &ExpectedLong, sizeof(ExpectedLong),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), currencyCol,
                              &ExpectedCurrency, sizeof(ExpectedCurrency),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), floatCol,
                              &ExpectedFloat, sizeof(ExpectedFloat),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), doubleCol,
                              &ExpectedDouble, sizeof(ExpectedDouble),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), textCol,
                              ExpectedText.data(), ExpectedText.size(),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), binaryCol,
                              ExpectedBinary.data(), ExpectedBinary.size(),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    //  Reattach in a fresh instance and read every column back.
    EseInstance instance(directory);
    EseSession session(instance);
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.string().c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), dbid, "Wide",
                           nullptr, 0, 0, &tableId));
    auto columnId = [&](const char* name) {
        JET_COLUMNDEF info = {};
        info.cbStruct = sizeof(info);
        CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId, name,
                                         &info, sizeof(info),
                                         JET_ColInfo));
        return info.columnid;
    };
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));

    uint8_t  observedBit = 0;
    uint8_t  observedUByte = 0;
    int16_t  observedShort = 0;
    int32_t  observedLong = 0;
    int64_t  observedCurrency = 0;
    float    observedFloat = 0.0f;
    double   observedDouble = 0.0;
    char     observedText[64] = {};
    uint8_t  observedBinary[ExpectedBinary.size()] = {};
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Bit"),
                               &observedBit, sizeof(observedBit),
                               &actualBytes, 0, nullptr));
    Require(observedBit == ExpectedBit);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("UByte"),
                               &observedUByte, sizeof(observedUByte),
                               &actualBytes, 0, nullptr));
    Require(observedUByte == ExpectedUByte);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Short"),
                               &observedShort, sizeof(observedShort),
                               &actualBytes, 0, nullptr));
    Require(observedShort == ExpectedShort);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Long"),
                               &observedLong, sizeof(observedLong),
                               &actualBytes, 0, nullptr));
    Require(observedLong == ExpectedLong);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Currency"),
                               &observedCurrency, sizeof(observedCurrency),
                               &actualBytes, 0, nullptr));
    Require(observedCurrency == ExpectedCurrency);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Float"),
                               &observedFloat, sizeof(observedFloat),
                               &actualBytes, 0, nullptr));
    Require(observedFloat == ExpectedFloat);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Double"),
                               &observedDouble, sizeof(observedDouble),
                               &actualBytes, 0, nullptr));
    Require(observedDouble == ExpectedDouble);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Text"),
                               observedText, sizeof(observedText),
                               &actualBytes, 0, nullptr));
    Require(std::string_view(observedText, actualBytes) == ExpectedText);
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               columnId("Binary"),
                               observedBinary, sizeof(observedBinary),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == ExpectedBinary.size());
    Require(std::memcmp(observedBinary, ExpectedBinary.data(),
                        ExpectedBinary.size()) == 0);
    CheckJet(JetCloseTable(session.Handle(), tableId));
    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                databasePath.string().c_str()));
}

//  ===================================================================
//  Tier::Regression — variable-length Binary column at boundary
//  sizes.  Catches refactors to variable-column length encoding
//  / boundary tracking on values at and near the 127, 255, and
//  page-boundary marks.
//  ===================================================================
EseIntegrationScenario(ColumnType, VariableLengthBinaryAcrossSizeBoundaries, Regression)
{
    TemporaryDirectory directory(
        "ColumnType.VariableLengthBinaryAcrossSizeBoundaries");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "VarBin.mdb");
    EseTable table(database, "VarBins");
    auto sizeColumn = table.AddColumn("Size", JET_coltypLong,
                                      JET_bitColumnNotNULL);
    auto bytesColumn = table.AddColumn("Bytes", JET_coltypLongBinary);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Size\0\0", 7);
    table.CreateIndex("PrimaryBySize", PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    const std::vector<int32_t> Sizes = {
        1, 2, 64, 126, 127, 128, 129, 254, 255, 256, 257, 1023, 1024,
        2048, 4095, 4096,
    };
    auto makeBytes = [](int32_t size) {
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        for (int32_t i = 0; i < size; ++i)
        {
            bytes[static_cast<size_t>(i)] =
                static_cast<uint8_t>((i * 53 + size) & 0xFF);
        }
        return bytes;
    };

    {
        EseTransaction transaction(session);
        for (int32_t size : Sizes)
        {
            const auto bytes = makeBytes(size);
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  sizeColumn, &size, sizeof(size),
                                  0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  bytesColumn,
                                  bytes.data(), bytes.size(),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    for (int32_t i = 0; i < static_cast<int32_t>(Sizes.size()); ++i)
    {
        int32_t observedSize = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   sizeColumn,
                                   &observedSize, sizeof(observedSize),
                                   &actualBytes, 0, nullptr));
        const auto expectedBytes = makeBytes(observedSize);
        std::vector<uint8_t> observedBytes(
            static_cast<size_t>(observedSize));
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   bytesColumn,
                                   observedBytes.data(), observedBytes.size(),
                                   &actualBytes, 0, nullptr));
        Require(static_cast<int32_t>(actualBytes) == observedSize);
        Require(std::memcmp(observedBytes.data(),
                            expectedBytes.data(),
                            expectedBytes.size()) == 0);
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (i + 1 == static_cast<int32_t>(Sizes.size()))
        {
            Require(moveResult == JET_errNoCurrentRecord);
        }
        else
        {
            CheckJet(moveResult);
        }
    }
}

//  ===================================================================
//  Tier::Regression — autoincrement is monotonic across detach +
//  reattach.  Smoke test only verifies monotonicity inside a single
//  session.  A refactor that lost the autoinc counter on JetTerm
//  would surface here as a duplicate value after reopen.
//  ===================================================================
EseIntegrationScenario(ColumnType, AutoincrementMonotonicAcrossReattach, Regression)
{
    TemporaryDirectory directory(
        "ColumnType.AutoincrementMonotonicAcrossReattach");
    const auto databasePath = directory.Path() / "Auto.mdb";

    static constexpr int32_t FirstBatch = 64;
    static constexpr int32_t SecondBatch = 32;

    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Auto.mdb");
        EseTable table(database, "Counted");
        table.AddColumn("Identity", JET_coltypLong,
                        JET_bitColumnAutoincrement);
        table.AddColumn("Filler", JET_coltypLong);
        EseTransaction transaction(session);
        for (int32_t i = 0; i < FirstBatch; ++i)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    EseInstance instance(directory);
    EseSession session(instance);
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.string().c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), dbid, "Counted",
                           nullptr, 0, 0, &tableId));
    JET_COLUMNDEF identityInfo = {};
    identityInfo.cbStruct = sizeof(identityInfo);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId,
                                     "Identity",
                                     &identityInfo, sizeof(identityInfo),
                                     JET_ColInfo));

    //  Read every existing Identity, capture the maximum.
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    int32_t maxIdentityBefore = 0;
    int32_t walkedRowCount = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                   identityInfo.columnid,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed > 0);
        if (observed > maxIdentityBefore)
        {
            maxIdentityBefore = observed;
        }
        ++walkedRowCount;
        const auto moveResult = JetMove(session.Handle(), tableId,
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walkedRowCount == FirstBatch);

    //  Insert SecondBatch more rows.  Every new Identity must
    //  exceed maxIdentityBefore — proves the counter survived
    //  the close+reopen.
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < SecondBatch; ++i)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), tableId,
                                      JET_prepInsert));
            CheckJet(JetUpdate(session.Handle(), tableId,
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    //  Final walk: total rows = FirstBatch+SecondBatch, strictly
    //  ascending Identity, every Identity > maxIdentityBefore for
    //  rows above index FirstBatch.
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    int32_t lastIdentity = 0;
    int32_t totalRows = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                   identityInfo.columnid,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed > lastIdentity);
        lastIdentity = observed;
        ++totalRows;
        const auto moveResult = JetMove(session.Handle(), tableId,
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(totalRows == FirstBatch + SecondBatch);
    Require(lastIdentity > maxIdentityBefore);
    CheckJet(JetCloseTable(session.Handle(), tableId));
    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                databasePath.string().c_str()));
}

