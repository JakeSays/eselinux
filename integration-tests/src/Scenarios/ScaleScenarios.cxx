// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Scale-aware scenarios.  Row counts, table counts, and LV sizes
// scale with the --scale Small|Medium|Large flag.  Small completes
// in well under a minute; Medium runs for a few minutes; Large can
// run for hours and is opt-in only.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/RowOperations.hxx"
#include "Framework/ScaleProfile.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <cstring>
#include <format>
#include <string>
#include <vector>

using namespace ese::tests;

EseIntegrationScenario(Scale, BulkInsertAndScan)
{
    TemporaryDirectory directory("Scale.BulkInsertAndScan");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Scale.mdb");
    EseTable table(database, "Bulk");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    const auto rowCount = RowCount();
    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    int observed = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        ++observed;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observed == rowCount);
}

EseIntegrationScenario(Scale, ManyTablesCreatedAndOpened)
{
    TemporaryDirectory directory("Scale.ManyTablesCreatedAndOpened");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Scale.mdb");

    const auto tableCount = TableCount();

    for (int index = 0; index < tableCount; ++index)
    {
        const auto name = std::format("Table{:05}", index);
        EseTable table(database, name);
        table.AddColumn("Value", JET_coltypLong);
    }

    for (int index = 0; index < tableCount; ++index)
    {
        const auto name = std::format("Table{:05}", index);
        EseTable opened(database, name, EseTableMode::Open);
        Require(opened.Id() != JET_tableidNil);
    }
}

EseIntegrationScenario(Scale, LargeLongValueRoundTrip)
{
    TemporaryDirectory directory("Scale.LargeLongValueRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Scale.mdb");
    EseTable table(database, "BigBlob");

    auto columnId = table.AddColumn("Blob", JET_coltypLongBinary);

    const auto longValueBytes = LongValueBytes();
    std::vector<uint8_t> writtenBytes(static_cast<size_t>(longValueBytes));
    for (size_t byteIndex = 0; byteIndex < writtenBytes.size(); ++byteIndex)
    {
        writtenBytes[byteIndex] = static_cast<uint8_t>((byteIndex * 19) & 0xFF);
    }

    {
        EseTransaction transaction(session);
        InsertSingleVariableColumnRow(table, columnId,
                                      writtenBytes.data(),
                                      static_cast<uint32_t>(writtenBytes.size()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBack =
        RetrieveVariableColumnFromCurrentRecord(table, columnId,
                                                static_cast<uint32_t>(longValueBytes));
    Require(readBack.size() == writtenBytes.size());
    Require(std::memcmp(readBack.data(),
                        writtenBytes.data(),
                        writtenBytes.size()) == 0);
}

EseIntegrationScenario(Scale, BulkInsertWithSecondaryIndexBuilds)
{
    TemporaryDirectory directory("Scale.BulkInsertWithSecondaryIndexBuilds");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Scale.mdb");
    EseTable table(database, "Indexed");

    auto keyColumnId = table.AddColumn("Key", JET_coltypLong, JET_bitColumnNotNULL);
    auto valueColumnId = table.AddColumn("Value", JET_coltypLong);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr std::string_view ValueKey =
        std::string_view("+Value\0\0", 8);
    table.CreateIndex("ByValue", ValueKey);

    const auto rowCount = RowCount();
    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
            const int32_t keyValue = rowIndex;
            const int32_t payloadValue = rowCount - rowIndex - 1;
            CheckJet(JetSetColumn(session.Handle(), table.Id(), keyColumnId,
                                  &keyValue, sizeof(keyValue), 0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), valueColumnId,
                                  &payloadValue, sizeof(payloadValue),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    // Walk the secondary index — values must come back in ascending
    // order even though we inserted them in descending Key order.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), "ByValue"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int32_t previousValue = 0;
    bool first = true;
    int observedRows = 0;
    while (true)
    {
        int32_t observedValue = 0;
        uint32_t actualSize = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), valueColumnId,
                                   &observedValue, sizeof(observedValue),
                                   &actualSize, 0, nullptr));
        if (!first)
        {
            Require(observedValue >= previousValue);
        }
        first = false;
        previousValue = observedValue;
        ++observedRows;
        const auto moveResult = JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(observedRows == rowCount);
}
