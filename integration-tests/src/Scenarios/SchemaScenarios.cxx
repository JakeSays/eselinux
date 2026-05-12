// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <cstring>

using namespace ese::tests;

EseIntegrationScenario(Schema, CreateTable)
{
    TemporaryDirectory directory("Schema.CreateTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    Require(table.Id() != JET_tableidNil);
}

EseIntegrationScenario(Schema, AddColumnAfterTableCreation)
{
    TemporaryDirectory directory("Schema.AddColumnAfterTableCreation");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    auto columnId = table.AddColumn("CustomerId", JET_coltypLong);
    Require(columnId != 0);
}

EseIntegrationScenario(Schema, AddMultipleColumnsRoundTripsThroughGetColumnInfo)
{
    TemporaryDirectory directory(
        "Schema.AddMultipleColumnsRoundTripsThroughGetColumnInfo");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    const auto identityColumnId = table.AddColumn("Identity",
                                                    JET_coltypLong,
                                                    JET_bitColumnAutoincrement);
    const auto nameColumnId = table.AddColumn("Name",
                                                    JET_coltypLongText,
                                                    0,
                                                    0,
                                                    1252);
    const auto rawBytesColumnId = table.AddColumn("RawBytes",
                                                    JET_coltypLongBinary);

    // Verify each column landed with the expected coltyp.
    auto VerifyColumn = [&](const char* columnName,
                            JET_COLUMNID expectedId,
                            JET_COLTYP expectedType)
    {
        JET_COLUMNDEF columnDefinition = {};
        columnDefinition.cbStruct = sizeof(columnDefinition);
        CheckJet(JetGetTableColumnInfoA(session.Handle(),
                                        table.Id(),
                                        columnName,
                                        &columnDefinition,
                                        sizeof(columnDefinition),
                                        JET_ColInfo));
        Require(columnDefinition.columnid == expectedId);
        Require(columnDefinition.coltyp == expectedType);
    };

    VerifyColumn("Identity", identityColumnId, JET_coltypLong);
    VerifyColumn("Name", nameColumnId, JET_coltypLongText);
    VerifyColumn("RawBytes", rawBytesColumnId, JET_coltypLongBinary);
}

EseIntegrationScenario(Schema, DeleteColumn)
{
    TemporaryDirectory directory("Schema.DeleteColumn");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    table.AddColumn("Keeper", JET_coltypLong);
    table.AddColumn("Disposable", JET_coltypLong);

    CheckJet(JetDeleteColumnA(session.Handle(), table.Id(), "Disposable"));

    // Retrieving info for the removed column must now return ColumnNotFound.
    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    RequireJetError(JetGetTableColumnInfoA(session.Handle(),
                                           table.Id(),
                                           "Disposable",
                                           &columnDefinition,
                                           sizeof(columnDefinition),
                                           JET_ColInfo),
                    JET_errColumnNotFound);

    // The keeper survives.
    CheckJet(JetGetTableColumnInfoA(session.Handle(),
                                    table.Id(),
                                    "Keeper",
                                    &columnDefinition,
                                    sizeof(columnDefinition),
                                    JET_ColInfo));
    Require(columnDefinition.coltyp == JET_coltypLong);
}

EseIntegrationScenario(Schema, RenameTable)
{
    TemporaryDirectory directory("Schema.RenameTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "OldName");

    table.AddColumn("Value", JET_coltypLong);

    // RenameTable requires the table to be closed first — the rename
    // operates on the database catalog, not the open cursor.
    CheckJet(JetCloseTable(session.Handle(), table.Id()));

    CheckJet(JetRenameTableA(session.Handle(),
                             database.Id(),
                             "OldName",
                             "NewName"));

    // Opening under the old name now fails, under the new name succeeds.
    JET_TABLEID transientTableId = JET_tableidNil;
    RequireJetError(JetOpenTableA(session.Handle(),
                                  database.Id(),
                                  "OldName",
                                  nullptr,
                                  0,
                                  0,
                                  &transientTableId),
                    JET_errObjectNotFound);

    CheckJet(JetOpenTableA(session.Handle(),
                           database.Id(),
                           "NewName",
                           nullptr,
                           0,
                           0,
                           &transientTableId));
    CheckJet(JetCloseTable(session.Handle(), transientTableId));
}

EseIntegrationScenario(Schema, RenameColumn)
{
    TemporaryDirectory directory("Schema.RenameColumn");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    auto columnId = table.AddColumn("OldColumnName", JET_coltypLong);

    CheckJet(JetRenameColumnA(session.Handle(),
                              table.Id(),
                              "OldColumnName",
                              "NewColumnName",
                              0));

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);

    RequireJetError(JetGetTableColumnInfoA(session.Handle(),
                                           table.Id(),
                                           "OldColumnName",
                                           &columnDefinition,
                                           sizeof(columnDefinition),
                                           JET_ColInfo),
                    JET_errColumnNotFound);

    CheckJet(JetGetTableColumnInfoA(session.Handle(),
                                    table.Id(),
                                    "NewColumnName",
                                    &columnDefinition,
                                    sizeof(columnDefinition),
                                    JET_ColInfo));
    Require(columnDefinition.columnid == columnId);
}

EseIntegrationScenario(Schema, CreatePrimaryIndex)
{
    TemporaryDirectory directory("Schema.CreatePrimaryIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    table.AddColumn("CustomerId",
                    JET_coltypLong,
                    JET_bitColumnAutoincrement | JET_bitColumnNotNULL);

    // Key descriptor: ascending on CustomerId, double-NUL terminated.
    static constexpr std::string_view PrimaryKey =
        std::string_view("+CustomerId\0\0", 13);
    table.CreateIndex("PrimaryByCustomerId",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    JET_INDEXLIST indexList = {};
    indexList.cbStruct = sizeof(indexList);
    CheckJet(JetGetTableIndexInfoA(session.Handle(),
                                   table.Id(),
                                   nullptr,
                                   &indexList,
                                   sizeof(indexList),
                                   JET_IdxInfoList));
    Require(indexList.cRecord >= 1);
    // Close the temp table JetGetTableIndexInfo opened for us.
    CheckJet(JetCloseTable(session.Handle(), indexList.tableid));
}

EseIntegrationScenario(Schema, CreateSecondaryUniqueIndex)
{
    TemporaryDirectory directory("Schema.CreateSecondaryUniqueIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    table.AddColumn("CustomerId", JET_coltypLong);
    table.AddColumn("EmailAddress", JET_coltypLongText, 0, 0, 1252);

    static constexpr std::string_view EmailKey =
        std::string_view("+EmailAddress\0\0", 15);
    table.CreateIndex("ByEmailAddressUnique",
                      EmailKey,
                      JET_bitIndexUnique | JET_bitIndexIgnoreNull);
}

EseIntegrationScenario(Schema, CreateMultiColumnIndex)
{
    TemporaryDirectory directory("Schema.CreateMultiColumnIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Sales");

    table.AddColumn("Region", JET_coltypLong);
    table.AddColumn("Quarter", JET_coltypLong);
    table.AddColumn("Total", JET_coltypCurrency);

    // "+Region\0+Quarter\0\0" — both ascending; double-NUL terminator.
    static constexpr std::string_view CompositeKey =
        std::string_view("+Region\0+Quarter\0\0", 18);
    table.CreateIndex("ByRegionQuarter", CompositeKey);
}

EseIntegrationScenario(Schema, DeleteIndex)
{
    TemporaryDirectory directory("Schema.DeleteIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers");

    table.AddColumn("CustomerId", JET_coltypLong);

    static constexpr std::string_view KeyDescriptor =
        std::string_view("+CustomerId\0\0", 13);
    table.CreateIndex("Disposable", KeyDescriptor);

    CheckJet(JetDeleteIndexA(session.Handle(), table.Id(), "Disposable"));

    // The deleted index must no longer be addressable.
    RequireJetError(JetSetCurrentIndexA(session.Handle(),
                                        table.Id(),
                                        "Disposable"),
                    JET_errIndexNotFound);
}

EseIntegrationScenario(Schema, CreateTableColumnIndexOneShot)
{
    TemporaryDirectory directory("Schema.CreateTableColumnIndexOneShot");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    JET_COLUMNCREATE_A columns[2] = { {}, {} };
    columns[0].cbStruct = sizeof(columns[0]);
    columns[0].szColumnName = const_cast<char*>("CustomerId");
    columns[0].coltyp = JET_coltypLong;
    columns[0].grbit = JET_bitColumnAutoincrement | JET_bitColumnNotNULL;

    columns[1].cbStruct = sizeof(columns[1]);
    columns[1].szColumnName = const_cast<char*>("Name");
    columns[1].coltyp = JET_coltypLongText;
    columns[1].cp = 1252;

    static constexpr std::string_view PrimaryKey =
        std::string_view("+CustomerId\0\0", 13);
    JET_INDEXCREATE_A indexes[1] = { {} };
    indexes[0].cbStruct = sizeof(indexes[0]);
    indexes[0].szIndexName = const_cast<char*>("PrimaryById");
    indexes[0].szKey = const_cast<char*>(PrimaryKey.data());
    indexes[0].cbKey = static_cast<uint32_t>(PrimaryKey.size());
    indexes[0].grbit = JET_bitIndexPrimary | JET_bitIndexUnique;
    indexes[0].ulDensity = 80;

    JET_TABLECREATE_A tableCreate = {};
    tableCreate.cbStruct = sizeof(tableCreate);
    tableCreate.szTableName = const_cast<char*>("Customers");
    tableCreate.ulPages = 16;
    tableCreate.ulDensity = 80;
    tableCreate.rgcolumncreate = columns;
    tableCreate.cColumns = 2;
    tableCreate.rgindexcreate = indexes;
    tableCreate.cIndexes = 1;

    CheckJet(JetCreateTableColumnIndexA(session.Handle(),
                                        database.Id(),
                                        &tableCreate));
    Require(tableCreate.tableid != JET_tableidNil);
    Require(columns[0].columnid != 0);
    Require(columns[1].columnid != 0);
    Require(tableCreate.cCreated == 4); // table + 2 columns + 1 index

    CheckJet(JetCloseTable(session.Handle(), tableCreate.tableid));
}

EseIntegrationScenario(Schema, GetTableInfoReportsCreationStats)
{
    TemporaryDirectory directory(
        "Schema.GetTableInfoReportsCreationStats");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Customers", EseTableMode::Create, 32, 80);

    table.AddColumn("CustomerId", JET_coltypLong);

    JET_OBJECTINFO objectInfo = {};
    objectInfo.cbStruct = sizeof(objectInfo);
    CheckJet(JetGetTableInfoA(session.Handle(),
                              table.Id(),
                              &objectInfo,
                              sizeof(objectInfo),
                              JET_TblInfo));
    Require(objectInfo.objtyp == JET_objtypTable);
}
