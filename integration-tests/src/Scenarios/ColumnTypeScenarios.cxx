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

using namespace ese::tests;

namespace
{

//  Round-trip one fixed-size value through a single-column table.
//  Each scenario opens its own directory / instance / session so
//  failures don't cross-contaminate.
template <typename ValueType>
void FixedColumnRoundTrip(const char* scenarioName,
                          JET_COLTYP  columnType,
                          ValueType   writtenValue)
{
    TemporaryDirectory directory(scenarioName);
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "ColumnType.mdb");
    EseTable           table(database, "RoundTrip");

    auto columnId = table.AddColumn("Value", columnType);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<ValueType>(table, columnId, writtenValue);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBack =
        RetrieveFixedColumnFromCurrentRecord<ValueType>(table, columnId);

    //  Compare as bytes so float/double NaN payloads round-trip too.
    Require(std::memcmp(&readBack, &writtenValue, sizeof(writtenValue)) == 0);
}

}  //  namespace

EseIntegrationScenario(ColumnType, BitRoundTrip)
{
    //  JET_coltypBit canonicalises non-zero inputs to 0xFF and zero
    //  inputs to 0x00 (fldmod.cxx around line 2535).  Test both the
    //  canonical "true" and canonical "false" round-trips explicitly.
    FixedColumnRoundTrip<uint8_t>("ColumnType.BitRoundTrip",
                                  JET_coltypBit,
                                  static_cast<uint8_t>(0xFF));
}

EseIntegrationScenario(ColumnType, BitZeroRoundTrip)
{
    FixedColumnRoundTrip<uint8_t>("ColumnType.BitZeroRoundTrip",
                                  JET_coltypBit,
                                  static_cast<uint8_t>(0));
}

EseIntegrationScenario(ColumnType, UnsignedByteRoundTrip)
{
    FixedColumnRoundTrip<uint8_t>("ColumnType.UnsignedByteRoundTrip",
                                  JET_coltypUnsignedByte,
                                  static_cast<uint8_t>(0xA5));
}

EseIntegrationScenario(ColumnType, ShortRoundTrip)
{
    FixedColumnRoundTrip<int16_t>("ColumnType.ShortRoundTrip",
                                  JET_coltypShort,
                                  static_cast<int16_t>(-12345));
}

EseIntegrationScenario(ColumnType, UnsignedShortRoundTrip)
{
    FixedColumnRoundTrip<uint16_t>("ColumnType.UnsignedShortRoundTrip",
                                   JET_coltypUnsignedShort,
                                   static_cast<uint16_t>(0xDEAD));
}

EseIntegrationScenario(ColumnType, LongRoundTrip)
{
    FixedColumnRoundTrip<int32_t>("ColumnType.LongRoundTrip",
                                  JET_coltypLong,
                                  static_cast<int32_t>(-2'000'000'000));
}

EseIntegrationScenario(ColumnType, UnsignedLongRoundTrip)
{
    FixedColumnRoundTrip<uint32_t>("ColumnType.UnsignedLongRoundTrip",
                                   JET_coltypUnsignedLong,
                                   static_cast<uint32_t>(0xCAFEBABEu));
}

EseIntegrationScenario(ColumnType, CurrencyRoundTrip)
{
    //  Currency is the engine's signed 8-byte fixed-point money type.
    FixedColumnRoundTrip<int64_t>("ColumnType.CurrencyRoundTrip",
                                  JET_coltypCurrency,
                                  static_cast<int64_t>(-1'234'567'890'123LL));
}

EseIntegrationScenario(ColumnType, LongLongRoundTrip)
{
    FixedColumnRoundTrip<int64_t>("ColumnType.LongLongRoundTrip",
                                  JET_coltypLongLong,
                                  static_cast<int64_t>(0x0123456789ABCDEFLL));
}

EseIntegrationScenario(ColumnType, UnsignedLongLongRoundTrip)
{
    FixedColumnRoundTrip<uint64_t>("ColumnType.UnsignedLongLongRoundTrip",
                                   JET_coltypUnsignedLongLong,
                                   static_cast<uint64_t>(0xDEADBEEF'FEEDFACEULL));
}

EseIntegrationScenario(ColumnType, SingleRoundTrip)
{
    FixedColumnRoundTrip<float>("ColumnType.SingleRoundTrip",
                                JET_coltypIEEESingle,
                                3.1415927f);
}

EseIntegrationScenario(ColumnType, DoubleRoundTrip)
{
    FixedColumnRoundTrip<double>("ColumnType.DoubleRoundTrip",
                                 JET_coltypIEEEDouble,
                                 2.718281828459045);
}

EseIntegrationScenario(ColumnType, DateTimeRoundTrip)
{
    //  DateTime is an 8-byte fractional-day count; round-trip the bit
    //  pattern directly to confirm the storage layer doesn't mutate it.
    union DateTimeBits
    {
        double  fractionalDays;
        uint8_t bytes[8];
    };
    DateTimeBits writtenValue = {};
    writtenValue.fractionalDays = 44'567.5;
    FixedColumnRoundTrip<DateTimeBits>("ColumnType.DateTimeRoundTrip",
                                       JET_coltypDateTime,
                                       writtenValue);
}

EseIntegrationScenario(ColumnType, GuidRoundTrip)
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

EseIntegrationScenario(ColumnType, BinaryRoundTrip)
{
    TemporaryDirectory directory("ColumnType.BinaryRoundTrip");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "ColumnType.mdb");
    EseTable           table(database, "BinaryRoundTrip");

    //  JET_coltypBinary caps at 255 bytes — write 200 to stay safely under.
    auto columnId = table.AddColumn("Bytes", JET_coltypBinary, 0, 255);

    static constexpr int        PayloadBytes = 200;
    std::vector<uint8_t>        writtenBytes(PayloadBytes);
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

EseIntegrationScenario(ColumnType, TextRoundTrip)
{
    TemporaryDirectory directory("ColumnType.TextRoundTrip");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "ColumnType.mdb");
    EseTable           table(database, "TextRoundTrip");

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

EseIntegrationScenario(ColumnType, LongBinaryRoundTrip)
{
    TemporaryDirectory directory("ColumnType.LongBinaryRoundTrip");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "ColumnType.mdb");
    EseTable           table(database, "LongBinaryRoundTrip");

    auto columnId = table.AddColumn("Bytes", JET_coltypLongBinary);

    static constexpr int        PayloadBytes = 8'192;
    std::vector<uint8_t>        writtenBytes(PayloadBytes);
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

EseIntegrationScenario(ColumnType, NotNullColumnRejectsNullInsert)
{
    TemporaryDirectory directory("ColumnType.NotNullColumnRejectsNullInsert");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "ColumnType.mdb");
    EseTable           table(database, "NotNullCheck");

    table.AddColumn("Required", JET_coltypLong, JET_bitColumnNotNULL);

    EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
    //  Skip setting the column and try to commit — JetUpdate must
    //  reject because the column is marked NOT NULL with no default.
    RequireJetError(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr),
                    JET_errNullInvalid);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepCancel));
}

EseIntegrationScenario(ColumnType, AutoincrementProducesMonotonicValues)
{
    TemporaryDirectory directory(
        "ColumnType.AutoincrementProducesMonotonicValues");
    EseInstance instance(directory);
    EseSession  session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable    table(database, "Auto");

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
    bool    first    = true;
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

EseIntegrationScenario(ColumnType, DefaultValueAppliedWhenColumnUnset)
{
    TemporaryDirectory directory(
        "ColumnType.DefaultValueAppliedWhenColumnUnset");
    EseInstance instance(directory);
    EseSession  session(instance);
    EseDatabase database(session, "ColumnType.mdb");
    EseTable    table(database, "Defaulted");

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
