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
    std::vector<JET_COLUMNID> columnIds;
    columnIds.reserve(IndexCount);
    for (int index = 0; index < IndexCount; ++index)
    {
        const auto name = std::format("Key{:02}", index);
        columnIds.push_back(table.AddColumn(name, JET_coltypLong));
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

    //  Insert a row whose KeyNN columns each carry a distinguishable
    //  value (NN * 100 + sentinel).  Then seek on every secondary
    //  index by its key value and confirm retrieval finds the row.
    //  Just calling SetCurrentIndex on each index proves dispatch
    //  works but doesn't prove the index is populated and seekable
    //  to actual data.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        for (int columnIndex = 0; columnIndex < IndexCount; ++columnIndex)
        {
            const int32_t value = columnIndex * 100 + 7;
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  columnIds[static_cast<size_t>(columnIndex)],
                                  &value, sizeof(value),
                                  0, nullptr));
        }
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }

    for (int index = 0; index < IndexCount; ++index)
    {
        const auto indexName = std::format("ByKey{:02}", index);
        CheckJet(JetSetCurrentIndexA(session.Handle(),
                                     table.Id(),
                                     indexName.c_str()));
        const int32_t seekValue = index * 100 + 7;
        CheckJet(JetMakeKey(session.Handle(), table.Id(),
                            &seekValue, sizeof(seekValue),
                            JET_bitNewKey));
        CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));
        int32_t observedValue = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   columnIds[static_cast<size_t>(index)],
                                   &observedValue, sizeof(observedValue),
                                   &actualBytes, 0, nullptr));
        Require(observedValue == seekValue);
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
        auto columnId = table.AddColumn("Value", JET_coltypLong);
        //  Insert a per-table sentinel so the reopen step can prove
        //  each table holds its OWN data — not just that 50 valid
        //  cursors exist.
        EseTransaction transaction(session);
        const int32_t sentinel = index + 1000;
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), columnId,
                              &sentinel, sizeof(sentinel),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }

    for (int index = 0; index < TableCount; ++index)
    {
        const auto name = std::format("Table{:03}", index);
        EseTable opened(database, name, EseTableMode::Open);
        Require(opened.Id() != JET_tableidNil);
        //  Read back the per-table sentinel — proves table N really
        //  is table N (not just "a valid table existed under some
        //  name we can open").
        JET_COLUMNDEF columnInfo = {};
        columnInfo.cbStruct = sizeof(columnInfo);
        CheckJet(JetGetTableColumnInfoA(session.Handle(), opened.Id(),
                                         "Value", &columnInfo,
                                         sizeof(columnInfo),
                                         JET_ColInfo));
        CheckJet(JetMove(session.Handle(), opened.Id(),
                         JET_MoveFirst, 0));
        int32_t observedSentinel = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), opened.Id(),
                                   columnInfo.columnid,
                                   &observedSentinel, sizeof(observedSentinel),
                                   &actualBytes, 0, nullptr));
        Require(observedSentinel == index + 1000);
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

    //  Close + reopen by the long name and round-trip a sentinel
    //  row — proves the engine stored the full 60-char name and
    //  resolves it on lookup, not just that the create accepted it.
    auto columnId = table.AddColumn("Value", JET_coltypLong);
    {
        EseTransaction transaction(session);
        const int32_t sentinel = 0xCAFE;
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), columnId,
                              &sentinel, sizeof(sentinel),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }
    CheckJet(JetCloseTable(session.Handle(), table.Id()));
    table.Release();

    JET_TABLEID reopened = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), database.Id(),
                           longName.c_str(), nullptr, 0, 0, &reopened));
    Require(reopened != JET_tableidNil);
    CheckJet(JetMove(session.Handle(), reopened, JET_MoveFirst, 0));
    int32_t observedSentinel = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), reopened, columnId,
                               &observedSentinel, sizeof(observedSentinel),
                               &actualBytes, 0, nullptr));
    Require(observedSentinel == 0xCAFE);
    CheckJet(JetCloseTable(session.Handle(), reopened));
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
