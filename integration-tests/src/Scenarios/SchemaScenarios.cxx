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
#include <string_view>

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

EseIntegrationScenario(Schema, DeleteTableRemovesTheTable)
{
    TemporaryDirectory directory("Schema.DeleteTableRemovesTheTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    {
        EseTable doomed(database, "Doomed");
        doomed.AddColumn("Value", JET_coltypLong);
        // EseTable closes its cursor at scope exit; JetDeleteTable
        // requires no open cursors on the target table.
    }

    CheckJet(JetDeleteTableA(session.Handle(), database.Id(), "Doomed"));

    // Subsequent open must surface ObjectNotFound — the table is gone.
    JET_TABLEID tableId = JET_tableidNil;
    RequireJetError(JetOpenTableA(session.Handle(), database.Id(),
                                  "Doomed", nullptr, 0, 0, &tableId),
                    JET_errObjectNotFound);
}

EseIntegrationScenario(Schema, DeleteTableOfUnknownTableReturnsObjectNotFound)
{
    TemporaryDirectory directory(
        "Schema.DeleteTableOfUnknownTableReturnsObjectNotFound");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    RequireJetError(JetDeleteTableA(session.Handle(),
                                    database.Id(),
                                    "NeverExisted"),
                    JET_errObjectNotFound);
}

EseIntegrationScenario(Schema, DeleteTableFreesTheNameForReuse)
{
    TemporaryDirectory directory("Schema.DeleteTableFreesTheNameForReuse");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    {
        EseTable original(database, "Reused");
        original.AddColumn("Value", JET_coltypLong);
    }
    CheckJet(JetDeleteTableA(session.Handle(), database.Id(), "Reused"));

    // After delete the name is free; recreating with a different schema
    // must succeed and report a fresh tableid.
    EseTable replacement(database, "Reused");
    const auto columnId = replacement.AddColumn("Different",
                                                JET_coltypLongText);
    Require(columnId != 0);
    Require(replacement.Id() != JET_tableidNil);
}

EseIntegrationScenario(Schema, GetObjectInfoReportsTableType)
{
    TemporaryDirectory directory("Schema.GetObjectInfoReportsTableType");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Inspectable");
    table.AddColumn("Value", JET_coltypLong);

    // JetGetObjectInfo with JET_ObjInfo + JET_OBJECTINFO struct
    // queries metadata for a single named object.
    JET_OBJECTINFO info = {};
    info.cbStruct = sizeof(info);
    CheckJet(JetGetObjectInfoA(session.Handle(), database.Id(),
                               JET_objtypTable,
                               /*szContainerName*/ nullptr,
                               "Inspectable",
                               &info, sizeof(info), JET_ObjInfo));
    Require(info.objtyp == JET_objtypTable);
    // grbit must include either updatable or bookmark — basic
    // metadata bits the engine always populates for user tables.
    Require((info.grbit & (JET_bitTableInfoUpdatable | JET_bitTableInfoBookmark))
            != 0);
}

EseIntegrationScenario(Schema, GetIndexInfoReturnsIndexCount)
{
    TemporaryDirectory directory("Schema.GetIndexInfoReturnsIndexCount");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Indexed");

    table.AddColumn("PrimaryKey",   JET_coltypLong, JET_bitColumnNotNULL);
    table.AddColumn("SecondaryKey", JET_coltypLong);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+PrimaryKey\0\0", 13);
    static constexpr std::string_view SecondaryKey =
        std::string_view("+SecondaryKey\0\0", 15);
    table.CreateIndex("Primary",   PrimaryKey,   JET_bitIndexPrimary);
    table.CreateIndex("Secondary", SecondaryKey);

    // JET_IdxInfoCount returns the total number of indexes on the
    // named table (primary + each secondary).  We created two.
    uint32_t indexCount = 0;
    CheckJet(JetGetIndexInfoA(session.Handle(), database.Id(),
                              "Indexed", /*szIndexName*/ nullptr,
                              &indexCount, sizeof(indexCount),
                              JET_IdxInfoCount));
    Require(indexCount == 2);
}

EseIntegrationScenario(Schema, CreateIndex2BuildsSecondaryIndex)
{
    TemporaryDirectory directory("Schema.CreateIndex2BuildsSecondaryIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Rows");

    auto identityColumnId = table.AddColumn(
        "Identity",
        JET_coltypLong,
        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto rankColumnId = table.AddColumn("Rank",
                                        JET_coltypLong,
                                        JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    //  JetCreateIndex2 builds N secondary indexes in one call using
    //  the JET_INDEXCREATE struct (vs the simpler szKey + grbit
    //  shape of v1).  Each entry sets cbStruct + szIndexName + szKey
    //  + cbKey + grbit + ulDensity.
    static constexpr std::string_view RankKey =
        std::string_view("+Rank\0\0", 7);
    JET_INDEXCREATE_A indexes[1] = { {} };
    indexes[0].cbStruct = sizeof(indexes[0]);
    indexes[0].szIndexName = const_cast<char*>("ByRank");
    indexes[0].szKey = const_cast<char*>(RankKey.data());
    indexes[0].cbKey = static_cast<uint32_t>(RankKey.size());
    indexes[0].grbit = 0;
    indexes[0].ulDensity = 80;

    CheckJet(JetCreateIndex2A(session.Handle(),
                              table.Id(),
                              indexes,
                              1));
    Require(indexes[0].err == JET_errSuccess);

    //  Populate rows in descending rank, then walk the new index —
    //  must come back in ascending rank order proving the index is
    //  built and active.
    constexpr int RowCount = 5;
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < RowCount; ++i)
        {
            const int32_t rank = RowCount - i;
            CheckJet(JetPrepareUpdate(session.Handle(),
                                      table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  rankColumnId,
                                  &rank,
                                  sizeof(rank),
                                  0,
                                  nullptr));
            CheckJet(JetUpdate(session.Handle(),
                               table.Id(),
                               nullptr,
                               0,
                               nullptr));
        }
        transaction.Commit();
    }

    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), "ByRank"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t previousRank = 0;
    for (int i = 0; i < RowCount; ++i)
    {
        const auto rank =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          rankColumnId);
        if (i > 0)
        {
            Require(rank > previousRank);
        }
        previousRank = rank;
        if (i + 1 < RowCount)
        {
            CheckJet(JetMove(session.Handle(),
                             table.Id(),
                             JET_MoveNext,
                             0));
        }
    }

    (void)identityColumnId;
}

EseIntegrationScenario(Schema, CreateDatabase2HonorsMaxPagesCap)
{
    TemporaryDirectory directory("Schema.CreateDatabase2HonorsMaxPagesCap");
    EseInstance instance(directory);
    EseSession session(instance);

    //  JetCreateDatabase2 takes a per-database cpgDatabaseSizeMax
    //  cap that the engine treats as a hard ceiling — equivalent to
    //  setting JET_dbparamDbSizeMaxPages.  256 pages = 1 MiB at 4 KiB
    //  pages, plenty for the small DDL we're going to add.
    const auto dbPath = (directory.Path() / "Capped.mdb").string();
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetCreateDatabase2A(session.Handle(),
                                 dbPath.c_str(),
                                 /*cpgDatabaseSizeMax=*/256,
                                 &dbid,
                                 0));
    Require(dbid != JET_dbidNil);

    //  Verify the cap was honored by reading it back via
    //  JetGetMaxDatabaseSize.
    uint32_t cappedPages = 0;
    CheckJet(JetGetMaxDatabaseSize(session.Handle(),
                                   dbid,
                                   &cappedPages,
                                   0));
    Require(cappedPages == 256);

    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(), dbPath.c_str()));
}

EseIntegrationScenario(Schema, AttachDatabase2AppliesMaxPagesCapOnAttach)
{
    TemporaryDirectory directory(
        "Schema.AttachDatabase2AppliesMaxPagesCapOnAttach");
    EseInstance instance(directory);
    EseSession session(instance);

    //  Phase A: create a database with no cap.
    const auto dbPath = (directory.Path() / "Reattach.mdb").string();
    {
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(session.Handle(),
                                    dbPath.c_str(),
                                    nullptr,
                                    &dbid,
                                    0));
        CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(), dbPath.c_str()));
    }

    //  Phase B: re-attach via JetAttachDatabase2 with a 512-page
    //  cap.  The cap is per-attach state, so a subsequent
    //  JetGetMaxDatabaseSize must report the new ceiling.
    CheckJet(JetAttachDatabase2A(session.Handle(),
                                 dbPath.c_str(),
                                 /*cpgDatabaseSizeMax=*/512,
                                 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              dbPath.c_str(),
                              nullptr,
                              &dbid,
                              0));

    uint32_t cappedPages = 0;
    CheckJet(JetGetMaxDatabaseSize(session.Handle(),
                                   dbid,
                                   &cappedPages,
                                   0));
    Require(cappedPages == 512);

    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(), dbPath.c_str()));
}

EseIntegrationScenario(Schema, CreateTableColumnIndex2BulkCreateRoundTrip)
{
    TemporaryDirectory directory(
        "Schema.CreateTableColumnIndex2BulkCreateRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    //  JET_TABLECREATE2 vs JET_TABLECREATE: same payload + extra
    //  szCallback / cbtyp fields for column-modify hooks.  Test the
    //  bulk-create entry by handing it the same DDL the v1 scenario
    //  uses, plus a NULL callback (we don't need it here).
    JET_COLUMNCREATE_A columns[2] = { {}, {} };
    columns[0].cbStruct = sizeof(columns[0]);
    columns[0].szColumnName = const_cast<char*>("Id");
    columns[0].coltyp = JET_coltypLong;
    columns[0].grbit = JET_bitColumnAutoincrement | JET_bitColumnNotNULL;

    columns[1].cbStruct = sizeof(columns[1]);
    columns[1].szColumnName = const_cast<char*>("Name");
    columns[1].coltyp = JET_coltypLongText;
    columns[1].cp = 1252;

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Id\0\0", 5);
    JET_INDEXCREATE_A indexes[1] = { {} };
    indexes[0].cbStruct = sizeof(indexes[0]);
    indexes[0].szIndexName = const_cast<char*>("PrimaryById");
    indexes[0].szKey = const_cast<char*>(PrimaryKey.data());
    indexes[0].cbKey = static_cast<uint32_t>(PrimaryKey.size());
    indexes[0].grbit = JET_bitIndexPrimary | JET_bitIndexUnique;
    indexes[0].ulDensity = 80;

    JET_TABLECREATE2_A create = {};
    create.cbStruct = sizeof(create);
    create.szTableName = const_cast<char*>("BulkV2");
    create.ulPages = 16;
    create.ulDensity = 80;
    create.rgcolumncreate = columns;
    create.cColumns = 2;
    create.rgindexcreate = indexes;
    create.cIndexes = 1;

    CheckJet(JetCreateTableColumnIndex2A(session.Handle(),
                                         database.Id(),
                                         &create));
    Require(create.tableid != JET_tableidNil);
    //  cCreated counts table + 2 columns + 1 index (no callback).
    Require(create.cCreated == 4);
    Require(columns[0].columnid != 0);
    Require(columns[1].columnid != 0);
    Require(indexes[0].err == JET_errSuccess);

    CheckJet(JetCloseTable(session.Handle(), create.tableid));
}

EseIntegrationScenario(Schema, ConvertDDLIncreasesMaxColumnSize)
{
    TemporaryDirectory directory("Schema.ConvertDDLIncreasesMaxColumnSize");
    EseInstance instance(directory);
    EseSession session(instance);
    const auto dbPath = (directory.Path() / "Schema.mdb").string();

    //  Phase A: create the database + a table with a Text column
    //  capped at InitialMax bytes.  Insert a row whose value
    //  exceeds the cap and confirm the engine silently truncates
    //  it (JET_wrnColumnMaxTruncated + read-back exactly cap-many
    //  bytes).  Detach the database when done — the catalog change
    //  in Phase B needs a fresh attach to invalidate the cached
    //  FCB/TDB that holds the in-memory cap.
    static constexpr uint32_t InitialMax = 16;
    static constexpr char LongValue[] =
        "abcdefghijklmnopqrstuvwxyz012345";  // 32 chars
    static_assert(sizeof(LongValue) - 1 > InitialMax);
    JET_COLUMNID columnId = 0;
    {
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(session.Handle(),
                                    dbPath.c_str(),
                                    nullptr,
                                    &dbid,
                                    0));

        JET_TABLEID tableid = JET_tableidNil;
        CheckJet(JetCreateTableA(session.Handle(),
                                 dbid,
                                 "Strings",
                                 16,
                                 80,
                                 &tableid));
        JET_COLUMNDEF coldef = {};
        coldef.cbStruct = sizeof(coldef);
        coldef.coltyp = JET_coltypText;
        coldef.cp = 1252;
        coldef.cbMax = InitialMax;
        coldef.grbit = JET_bitColumnNotNULL;
        CheckJet(JetAddColumnA(session.Handle(),
                               tableid,
                               "Value",
                               &coldef,
                               nullptr,
                               0,
                               &columnId));

        CheckJet(JetBeginTransaction(session.Handle()));
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  tableid,
                                  JET_prepInsert));
        const auto setErr = JetSetColumn(session.Handle(),
                                         tableid,
                                         columnId,
                                         LongValue,
                                         sizeof(LongValue) - 1,
                                         0,
                                         nullptr);
        Require(setErr == JET_wrnColumnMaxTruncated);
        CheckJet(JetUpdate(session.Handle(), tableid, nullptr, 0, nullptr));
        CheckJet(JetCommitTransaction(session.Handle(), 0));

        CheckJet(JetMove(session.Handle(), tableid, JET_MoveFirst, 0));
        char preReadBack[128] = {};
        uint32_t cbActual = 0;
        CheckJet(JetRetrieveColumn(session.Handle(),
                                   tableid,
                                   columnId,
                                   preReadBack,
                                   sizeof(preReadBack),
                                   &cbActual,
                                   0,
                                   nullptr));
        Require(cbActual == InitialMax);

        CheckJet(JetCloseTable(session.Handle(), tableid));
        CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(), dbPath.c_str()));
    }

    //  Phase B: reattach.  Now apply ConvertDDL to raise the cap.
    //  ErrCATIncreaseMaxColumnSize writes the new value to the
    //  catalog row.  Detach again to invalidate the FCB cache
    //  before Phase C reattaches with the new cap visible.
    {
        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    dbPath.c_str(),
                                    0));
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(session.Handle(),
                                  dbPath.c_str(),
                                  nullptr,
                                  &dbid,
                                  0));

        JET_DDLMAXCOLUMNSIZE_A increase = {};
        increase.szTable = const_cast<char*>("Strings");
        increase.szColumn = const_cast<char*>("Value");
        increase.cbMax = 128;
        CheckJet(JetConvertDDLA(session.Handle(),
                                dbid,
                                opDDLConvIncreaseMaxColumnSize,
                                &increase,
                                sizeof(increase)));

        CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(), dbPath.c_str()));
    }

    //  Phase C: reattach with a fresh FCB.  The full 32-byte
    //  string now round-trips without truncation.
    {
        CheckJet(JetAttachDatabaseA(session.Handle(),
                                    dbPath.c_str(),
                                    0));
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(session.Handle(),
                                  dbPath.c_str(),
                                  nullptr,
                                  &dbid,
                                  0));
        JET_TABLEID tableid = JET_tableidNil;
        CheckJet(JetOpenTableA(session.Handle(),
                               dbid,
                               "Strings",
                               nullptr,
                               0,
                               0,
                               &tableid));

        CheckJet(JetBeginTransaction(session.Handle()));
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  tableid,
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(),
                              tableid,
                              columnId,
                              LongValue,
                              sizeof(LongValue) - 1,
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(), tableid, nullptr, 0, nullptr));
        CheckJet(JetCommitTransaction(session.Handle(), 0));

        CheckJet(JetMove(session.Handle(), tableid, JET_MoveLast, 0));
        char postReadBack[128] = {};
        uint32_t cbActual = 0;
        CheckJet(JetRetrieveColumn(session.Handle(),
                                   tableid,
                                   columnId,
                                   postReadBack,
                                   sizeof(postReadBack),
                                   &cbActual,
                                   0,
                                   nullptr));
        Require(cbActual == sizeof(LongValue) - 1);
        Require(std::memcmp(postReadBack, LongValue, cbActual) == 0);

        CheckJet(JetCloseTable(session.Handle(), tableid));
        CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(), dbPath.c_str()));
    }
}

EseIntegrationScenario(Schema, ConvertDDLChangesIndexDensity)
{
    TemporaryDirectory directory("Schema.ConvertDDLChangesIndexDensity");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Key",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    //  Bump density to 95 via JetConvertDDL.  Routes through
    //  ErrCATChangeIndexDensity which updates the catalog row for
    //  this index; existing pages are not restructured, but the
    //  new value affects future page splits.
    JET_DDLINDEXDENSITY_A change = {};
    change.szTable = const_cast<char*>("Rows");
    change.szIndex = const_cast<char*>("PrimaryByKey");
    change.ulDensity = 95;
    CheckJet(JetConvertDDLA(session.Handle(),
                            database.Id(),
                            opDDLConvChangeIndexDensity,
                            &change,
                            sizeof(change)));

    //  Insert + scan still works — verify the post-change index
    //  is usable.
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 50; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int seen = 0;
    while (true)
    {
        ++seen;
        const auto rc = JetMove(session.Handle(),
                                table.Id(),
                                JET_MoveNext,
                                0);
        if (rc == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(rc);
    }
    Require(seen == 50);
}
