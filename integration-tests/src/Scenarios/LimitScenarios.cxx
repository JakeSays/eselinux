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

#include <format>
#include <string>
#include <vector>

using namespace ese::tests;

EseIntegrationScenario(Limit, TableWithOneHundredColumnsRoundTrips)
{
    TemporaryDirectory directory("Limit.TableWithOneHundredColumnsRoundTrips");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Limit.mdb");
    EseTable table(database, "Wide");

    static constexpr int ColumnCount = 100;
    std::vector<JET_COLUMNID> columnIdentifiers;
    columnIdentifiers.reserve(ColumnCount);
    for (int index = 0; index < ColumnCount; ++index)
    {
        const auto name = std::format("Column{:03}", index);
        columnIdentifiers.push_back(
            table.AddColumn(name, JET_coltypLong, JET_bitColumnNotNULL));
    }

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        for (int index = 0; index < ColumnCount; ++index)
        {
            const int32_t value = index;
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  columnIdentifiers[static_cast<size_t>(index)],
                                  &value, sizeof(value), 0, nullptr));
        }
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    for (int index = 0; index < ColumnCount; ++index)
    {
        const auto value =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(
                table, columnIdentifiers[static_cast<size_t>(index)]);
        Require(value == index);
    }
}

EseIntegrationScenario(Limit, TableWithTwentyIndexesRoundTrips)
{
    TemporaryDirectory directory("Limit.TableWithTwentyIndexesRoundTrips");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Limit.mdb");
    EseTable table(database, "ManyIndexes");

    static constexpr int IndexCount = 20;
    for (int index = 0; index < IndexCount; ++index)
    {
        const auto name = std::format("Key{:02}", index);
        table.AddColumn(name, JET_coltypLong);
    }

    for (int index = 0; index < IndexCount; ++index)
    {
        const auto indexName = std::format("ByKey{:02}", index);
        // std::format strips trailing NULs from the literal; the JET
        // key spec needs an explicit double-NUL terminator.
        std::string keyBuffer = std::format("+Key{:02}", index);
        keyBuffer.push_back('\0');
        keyBuffer.push_back('\0');
        table.CreateIndex(indexName, keyBuffer);
    }

    for (int index = 0; index < IndexCount; ++index)
    {
        const auto indexName = std::format("ByKey{:02}", index);
        CheckJet(JetSetCurrentIndexA(session.Handle(),
                                     table.Id(),
                                     indexName.c_str()));
    }
}

EseIntegrationScenario(Limit, FiftyTablesInOneDatabase)
{
    TemporaryDirectory directory("Limit.FiftyTablesInOneDatabase");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Limit.mdb");

    static constexpr int TableCount = 50;
    for (int index = 0; index < TableCount; ++index)
    {
        const auto name = std::format("Table{:03}", index);
        EseTable table(database, name);
        table.AddColumn("Value", JET_coltypLong);
    }

    for (int index = 0; index < TableCount; ++index)
    {
        const auto name = std::format("Table{:03}", index);
        EseTable opened(database, name, EseTableMode::Open);
        Require(opened.Id() != JET_tableidNil);
    }
}

EseIntegrationScenario(Limit, LongTableNameAccepted)
{
    TemporaryDirectory directory("Limit.LongTableNameAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Limit.mdb");

    // ESE table names are capped around 64 chars; 60 lands under that.
    const std::string longName(60, 'A');
    EseTable table(database, longName);
    Require(table.Id() != JET_tableidNil);
}

EseIntegrationScenario(Limit, ColumnNameAtMaximumLengthAccepted)
{
    TemporaryDirectory directory("Limit.ColumnNameAtMaximumLengthAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Limit.mdb");
    EseTable table(database, "Names");

    const std::string longColumnName(60, 'C');
    const auto columnId = table.AddColumn(longColumnName, JET_coltypLong);
    Require(columnId != 0);

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), table.Id(),
                                    longColumnName.c_str(),
                                    &columnDefinition,
                                    sizeof(columnDefinition),
                                    JET_ColInfo));
    Require(columnDefinition.columnid == columnId);
}

EseIntegrationScenario(Limit, EmptyTableNameRejected)
{
    TemporaryDirectory directory("Limit.EmptyTableNameRejected");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Limit.mdb");

    JET_TABLEID tableId = JET_tableidNil;
    RequireJetError(JetCreateTableA(session.Handle(), database.Id(),
                                    "", 16, 80, &tableId),
                    JET_errInvalidName);
}
