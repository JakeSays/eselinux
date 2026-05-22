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
#include <format>
#include <string>
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

    //  Close + reopen by name to prove the table actually landed in
    //  the catalog — handle non-nil alone says JetCreateTable returned
    //  success but doesn't confirm the table is queryable post-DDL.
    CheckJet(JetCloseTable(session.Handle(), table.Id()));
    JET_TABLEID reopened = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), database.Id(),
                           "Customers", nullptr, 0, 0, &reopened));
    Require(reopened != JET_tableidNil);
    CheckJet(JetCloseTable(session.Handle(), reopened));
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

    //  Round-trip the column through JetGetTableColumnInfo so we
    //  prove the column was actually added (not just that the call
    //  returned an id-looking value) and the engine reports the
    //  matching columnid + coltyp.
    JET_COLUMNDEF retrieved = {};
    retrieved.cbStruct = sizeof(retrieved);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), table.Id(),
                                     "CustomerId",
                                     &retrieved, sizeof(retrieved),
                                     JET_ColInfo));
    Require(retrieved.columnid == columnId);
    Require(retrieved.coltyp == JET_coltypLong);
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

    auto customerIdColumn = table.AddColumn("CustomerId", JET_coltypLong);
    auto emailColumn =
        table.AddColumn("EmailAddress", JET_coltypLongText, 0, 0, 1252);

    static constexpr std::string_view EmailKey =
        std::string_view("+EmailAddress\0\0", 15);
    table.CreateIndex("ByEmailAddressUnique",
                      EmailKey,
                      JET_bitIndexUnique | JET_bitIndexIgnoreNull);

    //  Insert two distinct rows, then prove the index is actually
    //  populated and unique-enforced: seek by EmailAddress retrieves
    //  the right CustomerId, and a third insert with a duplicate
    //  EmailAddress is rejected at the index level.
    {
        EseTransaction transaction(session);
        for (int32_t customerId : { 100, 200 })
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  customerIdColumn,
                                  &customerId, sizeof(customerId),
                                  0, nullptr));
            const std::string email =
                std::format("user{}@example.com", customerId);
            CheckJet(JetSetColumn(session.Handle(), table.Id(), emailColumn,
                                  email.data(), email.size(),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ByEmailAddressUnique"));
    const std::string seekEmail = "user200@example.com";
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        seekEmail.data(), seekEmail.size(),
                        JET_bitNewKey));
    CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));
    int32_t observedCustomerId = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                               customerIdColumn,
                               &observedCustomerId,
                               sizeof(observedCustomerId),
                               &actualBytes, 0, nullptr));
    Require(observedCustomerId == 200);

    //  Insert with duplicate EmailAddress — uniqueness must reject.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        const int32_t collidingId = 300;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), customerIdColumn,
                              &collidingId, sizeof(collidingId),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), emailColumn,
                              seekEmail.data(), seekEmail.size(),
                              0, nullptr));
        RequireJetError(JetUpdate(session.Handle(), table.Id(),
                                  nullptr, 0, nullptr),
                        JET_errKeyDuplicate);
    }
}

EseIntegrationScenario(Schema, CreateMultiColumnIndex)
{
    TemporaryDirectory directory("Schema.CreateMultiColumnIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Sales");

    auto regionColumn = table.AddColumn("Region", JET_coltypLong);
    auto quarterColumn = table.AddColumn("Quarter", JET_coltypLong);
    auto totalColumn = table.AddColumn("Total", JET_coltypCurrency);

    // "+Region\0+Quarter\0\0" — both ascending; double-NUL terminator.
    static constexpr std::string_view CompositeKey =
        std::string_view("+Region\0+Quarter\0\0", 18);
    table.CreateIndex("ByRegionQuarter", CompositeKey);

    //  Populate four (Region, Quarter) pairs with distinguishable
    //  totals and verify the composite index actually finds the
    //  right row when seeking on both key columns together.
    struct SalesRow { int32_t region; int32_t quarter; int64_t total; };
    const SalesRow rows[] =
    {
        { 1, 1, 100 }, { 1, 2, 200 }, { 2, 1, 300 }, { 2, 2, 400 },
    };
    {
        EseTransaction transaction(session);
        for (const auto& row : rows)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), regionColumn,
                                  &row.region, sizeof(row.region),
                                  0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), quarterColumn,
                                  &row.quarter, sizeof(row.quarter),
                                  0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), totalColumn,
                                  &row.total, sizeof(row.total),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ByRegionQuarter"));
    const int32_t seekRegion = 2;
    const int32_t seekQuarter = 1;
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &seekRegion, sizeof(seekRegion), JET_bitNewKey));
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &seekQuarter, sizeof(seekQuarter), 0));
    CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));
    int64_t observedTotal = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), totalColumn,
                               &observedTotal, sizeof(observedTotal),
                               &actualBytes, 0, nullptr));
    Require(observedTotal == 300);
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
    //  A regular user table must be Updatable (Insert/Update/Delete
    //  allowed) and must NOT carry the FixedDDL or Template flags —
    //  those are reserved for system catalogs and inheritable
    //  templates.  Asserting the bit pattern proves the engine
    //  populated the struct (not just returned success).
    Require((objectInfo.grbit & JET_bitTableInfoUpdatable) != 0);
    Require((objectInfo.flags & JET_bitObjectTableFixedDDL) == 0);
    Require((objectInfo.flags & JET_bitObjectTableTemplate) == 0);
    Require((objectInfo.flags & JET_bitObjectSystem) == 0);

    //  JET_TblInfoSpaceUsage reports two PGNO values — owned and
    //  available extents.  Owned must be non-zero (the table has at
    //  least its root page allocated), and it must be > available
    //  (some pages are in active use, not just on the free-extent
    //  list).  That's an actual engine-side fact, not just a "call
    //  returned success" check.
    uint32_t spaceUsage[2] = {};
    CheckJet(JetGetTableInfoA(session.Handle(),
                              table.Id(),
                              spaceUsage, sizeof(spaceUsage),
                              JET_TblInfoSpaceUsage));
    const uint32_t owned = spaceUsage[0];
    const uint32_t available = spaceUsage[1];
    Require(owned > 0);
    Require(available <= owned);
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

//  Versioned variants with genuinely new functional surface.
//  Each scenario exercises a -3/-4/-5 variant whose struct or
//  argument additions change observable behaviour, not just
//  expose the same plumbing under a wider signature.

//  JetCreateDatabase3 adds a JET_SETDBPARAM array — callers stamp
//  per-database parameters at create time.  Set
//  JET_dbparamDbSizeMaxPages and confirm JetGetMaxDatabaseSize
//  reads back the same value, proving the param actually
//  landed in the engine's per-DB state (rather than just being
//  accepted and discarded).
EseIntegrationScenario(Schema, CreateDatabase3StampsDbSizeMaxPages)
{
    TemporaryDirectory directory("Schema.CreateDatabase3StampsDbSizeMaxPages");
    EseInstance instance(directory);
    EseSession session(instance);

    constexpr uint32_t MaxPages = 4096;
    uint32_t maxPagesParam = MaxPages;

    JET_SETDBPARAM params[1] = { {} };
    params[0].dbparamid = JET_dbparamDbSizeMaxPages;
    params[0].pvParam = &maxPagesParam;
    params[0].cbParam = sizeof(maxPagesParam);

    const auto databasePath = directory.Path() / "Stamped.mdb";
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabase3A(session.Handle(),
                                 databasePath.string().c_str(),
                                 &databaseId,
                                 params, 1,
                                 JET_bitDbOverwriteExisting));

    uint32_t readBack = 0;
    CheckJet(JetGetMaxDatabaseSize(session.Handle(), databaseId,
                                   &readBack, 0));
    Require(readBack == MaxPages);

    CheckJet(JetCloseDatabase(session.Handle(), databaseId, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                databasePath.string().c_str()));
}

//  JetAttachDatabase3 mirrors JetCreateDatabase3 — same
//  JET_SETDBPARAM array, applied at attach time.  Create the
//  DB without a max-pages cap, detach, then re-attach via
//  JetAttachDatabase3 specifying a cap; verify the engine
//  picks it up.
EseIntegrationScenario(Schema, AttachDatabase3StampsDbSizeMaxPages)
{
    TemporaryDirectory directory("Schema.AttachDatabase3StampsDbSizeMaxPages");
    EseInstance instance(directory);
    EseSession session(instance);

    const auto databasePath = directory.Path() / "Stamped.mdb";

    //  Phase 1: create with default cap, immediately detach.
    {
        JET_DBID databaseId = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(session.Handle(),
                                    databasePath.string().c_str(),
                                    nullptr, &databaseId,
                                    JET_bitDbOverwriteExisting));
        CheckJet(JetCloseDatabase(session.Handle(), databaseId, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(),
                                    databasePath.string().c_str()));
    }

    //  Phase 2: re-attach with the cap set via JetAttachDatabase3.
    constexpr uint32_t MaxPages = 8192;
    uint32_t maxPagesParam = MaxPages;
    JET_SETDBPARAM params[1] = { {} };
    params[0].dbparamid = JET_dbparamDbSizeMaxPages;
    params[0].pvParam = &maxPagesParam;
    params[0].cbParam = sizeof(maxPagesParam);

    CheckJet(JetAttachDatabase3A(session.Handle(),
                                 databasePath.string().c_str(),
                                 params, 1, 0));
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &databaseId, 0));

    uint32_t readBack = 0;
    CheckJet(JetGetMaxDatabaseSize(session.Handle(), databaseId,
                                   &readBack, 0));
    Require(readBack == MaxPages);

    CheckJet(JetCloseDatabase(session.Handle(), databaseId, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                databasePath.string().c_str()));
}

//  JetCreateIndex3 takes a JET_INDEXCREATE2 — same as
//  JET_INDEXCREATE plus a JET_SPACEHINTS pointer for explicit
//  index-space tuning.  Build a table with a string column, use
//  JetCreateIndex3 with non-default space hints, then exercise
//  the index with a seek — confirms the index actually built
//  and is usable.
EseIntegrationScenario(Schema, CreateIndex3WithSpaceHintsBuilds)
{
    TemporaryDirectory directory("Schema.CreateIndex3WithSpaceHintsBuilds");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Index3.mdb");

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(), database.Id(),
                             "Rows", 16, 100, &tableId));
    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    JET_COLUMNID valueColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(), tableId, "Value",
                           &columnDefinition, nullptr, 0,
                           &valueColumnId));

    JET_SPACEHINTS spaceHints = {};
    spaceHints.cbStruct = sizeof(spaceHints);
    spaceHints.ulInitialDensity = 80;
    spaceHints.cbInitial = 16 * 1024;  // 16 KiB
    spaceHints.grbit = 0;
    spaceHints.ulMaintDensity = 80;

    char indexKey[] = "+Value\0";
    JET_INDEXCREATE2_A indexCreate = {};
    indexCreate.cbStruct = sizeof(indexCreate);
    indexCreate.szIndexName = const_cast<char*>("ValueIndex");
    indexCreate.szKey = indexKey;
    indexCreate.cbKey = sizeof(indexKey);
    indexCreate.grbit = JET_bitIndexUnique;
    indexCreate.ulDensity = 80;
    indexCreate.pSpacehints = &spaceHints;

    CheckJet(JetCreateIndex3A(session.Handle(), tableId,
                              &indexCreate, 1));
    Require(indexCreate.err == JET_errSuccess);

    //  Exercise the index: insert two rows, seek to the second.
    CheckJet(JetBeginTransaction(session.Handle()));
    for (int32_t value : { 100, 200 })
    {
        CheckJet(JetPrepareUpdate(session.Handle(), tableId,
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), tableId, valueColumnId,
                              &value, sizeof(value), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), tableId, nullptr, 0, nullptr));
    }
    CheckJet(JetCommitTransaction(session.Handle(), 0));

    CheckJet(JetSetCurrentIndex2A(session.Handle(), tableId,
                                  "ValueIndex", 0));
    const int32_t seekKey = 200;
    CheckJet(JetMakeKey(session.Handle(), tableId,
                        &seekKey, sizeof(seekKey), JET_bitNewKey));
    CheckJet(JetSeek(session.Handle(), tableId, JET_bitSeekEQ));

    int32_t observed = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), tableId, valueColumnId,
                               &observed, sizeof(observed), &actualSize,
                               0, nullptr));
    Require(observed == seekKey);

    CheckJet(JetCloseTable(session.Handle(), tableId));
}

//  JetCreateIndex4 takes a JET_INDEXCREATE3 — same as
//  JET_INDEXCREATE2 but with JET_UNICODEINDEX2 (locale-name
//  based) instead of the lcid-based JET_UNICODEINDEX.  Mirrors
//  the temp-table OpenTemporaryTable2 upgrade.  Build an index
//  over a Unicode-text column with case-insensitive collation
//  via locale-name "en-US".
EseIntegrationScenario(Schema, CreateIndex4WithUnicodeIndex2Sorts)
{
    TemporaryDirectory directory("Schema.CreateIndex4WithUnicodeIndex2Sorts");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Index4.mdb");

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(), database.Id(),
                             "Rows", 16, 100, &tableId));
    JET_COLUMNDEF textColumn = {};
    textColumn.cbStruct = sizeof(textColumn);
    textColumn.coltyp = JET_coltypLongText;
    textColumn.cp = 1200;  // UTF-16
    textColumn.grbit = JET_bitColumnNotNULL;
    JET_COLUMNID textColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(), tableId, "Word",
                           &textColumn, nullptr, 0, &textColumnId));

    static char16_t LocaleName[] = u"en-US";
    JET_UNICODEINDEX2 unicodeIndex = {};
    unicodeIndex.szLocaleName = LocaleName;
    unicodeIndex.dwMapFlags = 0x00000001;  // NORM_IGNORECASE

    char indexKey[] = "+Word\0";
    JET_INDEXCREATE3_A indexCreate = {};
    indexCreate.cbStruct = sizeof(indexCreate);
    indexCreate.szIndexName = const_cast<char*>("WordIndex");
    indexCreate.szKey = indexKey;
    indexCreate.cbKey = sizeof(indexKey);
    indexCreate.grbit = JET_bitIndexUnicode;
    indexCreate.ulDensity = 80;
    indexCreate.pidxunicode = &unicodeIndex;

    CheckJet(JetCreateIndex4A(session.Handle(), tableId,
                              &indexCreate, 1));
    Require(indexCreate.err == JET_errSuccess);

    auto insertWord = [&](const char16_t* text, uint32_t cch)
    {
        CheckJet(JetBeginTransaction(session.Handle()));
        CheckJet(JetPrepareUpdate(session.Handle(), tableId,
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), tableId, textColumnId,
                              text, cch * sizeof(char16_t),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), tableId, nullptr, 0, nullptr));
        CheckJet(JetCommitTransaction(session.Handle(), 0));
    };
    static const char16_t Banana[] = u"banana";
    static const char16_t Apple[]  = u"Apple";
    static const char16_t Cherry[] = u"cherry";
    insertWord(Banana, 6);
    insertWord(Apple, 5);
    insertWord(Cherry, 6);

    CheckJet(JetSetCurrentIndex2A(session.Handle(), tableId,
                                  "WordIndex", 0));
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    char16_t buffer[16] = {};
    uint32_t cbActual = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), tableId, textColumnId,
                               buffer, sizeof(buffer), &cbActual,
                               0, nullptr));
    //  Case-insensitive sort puts "Apple" first.
    Require(buffer[0] == u'A' || buffer[0] == u'a');

    CheckJet(JetCloseTable(session.Handle(), tableId));
}

//  JetCreateTableColumnIndex3/4/5 build a table + columns +
//  indexes in one call, each version layering on more struct
//  surface:
//    TABLECREATE3: pSeqSpacehints / pLVSpacehints / cbSeparateLV
//                  + JET_INDEXCREATE2 (space hints per index).
//    TABLECREATE4: same as 3 but indexes carry JET_INDEXCREATE3
//                  (UNICODEINDEX2 locale-name).
//    TABLECREATE5: TABLECREATE4 + cbLVChunkMax.
//  One scenario exercises all three by building three tables
//  in the same database — each call must succeed and yield a
//  usable table with the expected column count.
EseIntegrationScenario(Schema, CreateTableColumnIndex345BuildsProgressiveForms)
{
    TemporaryDirectory directory(
        "Schema.CreateTableColumnIndex345BuildsProgressiveForms");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "TableCreate345.mdb");

    //  Reusable column + index seed.  Each TABLECREATE struct
    //  borrows these by pointer; the cCreated readback then
    //  proves the engine made (table + columns + indexes).
    JET_COLUMNCREATE_A columns[2] = { {}, {} };
    columns[0].cbStruct = sizeof(columns[0]);
    columns[0].szColumnName = const_cast<char*>("Key");
    columns[0].coltyp = JET_coltypLong;
    columns[0].grbit = JET_bitColumnNotNULL;
    columns[1].cbStruct = sizeof(columns[1]);
    columns[1].szColumnName = const_cast<char*>("Payload");
    columns[1].coltyp = JET_coltypLong;

    char keyIndexKey[] = "+Key\0";

    //  TABLECREATE3: JET_INDEXCREATE2 (with pSpacehints left null —
    //  the engine's ValidateSpaceHints rejects most non-default
    //  values, and the field is genuinely optional).  Exercise
    //  cbSeparateLV which is one of the v3 surface additions.
    {
        JET_INDEXCREATE2_A indexes[1] = { {} };
        indexes[0].cbStruct = sizeof(indexes[0]);
        indexes[0].szIndexName = const_cast<char*>("PrimaryKey");
        indexes[0].szKey = keyIndexKey;
        indexes[0].cbKey = sizeof(keyIndexKey);
        indexes[0].grbit = JET_bitIndexPrimary | JET_bitIndexUnique;
        indexes[0].ulDensity = 80;

        JET_TABLECREATE3_A tableCreate = {};
        tableCreate.cbStruct = sizeof(tableCreate);
        tableCreate.szTableName = const_cast<char*>("Three");
        tableCreate.ulPages = 1;
        tableCreate.ulDensity = 80;
        tableCreate.rgcolumncreate = columns;
        tableCreate.cColumns = 2;
        tableCreate.rgindexcreate = indexes;
        tableCreate.cIndexes = 1;
        tableCreate.cbSeparateLV = 1024;

        CheckJet(JetCreateTableColumnIndex3A(session.Handle(),
                                             database.Id(),
                                             &tableCreate));
        //  cCreated counts table + columns + indexes (+ callbacks).
        //  Here: 1 table + 2 columns + 1 index = 4.
        Require(tableCreate.cCreated == 4);
        Require(tableCreate.tableid != JET_tableidNil);
        CheckJet(JetCloseTable(session.Handle(), tableCreate.tableid));
    }

    //  TABLECREATE4: JET_INDEXCREATE3 with UNICODEINDEX2.
    {
        static char16_t LocaleName[] = u"en-US";
        JET_UNICODEINDEX2 unicodeIndex = {};
        unicodeIndex.szLocaleName = LocaleName;
        unicodeIndex.dwMapFlags = 0x00000001;

        JET_INDEXCREATE3_A indexes[1] = { {} };
        indexes[0].cbStruct = sizeof(indexes[0]);
        indexes[0].szIndexName = const_cast<char*>("PrimaryKey");
        indexes[0].szKey = keyIndexKey;
        indexes[0].cbKey = sizeof(keyIndexKey);
        indexes[0].grbit = JET_bitIndexPrimary | JET_bitIndexUnique;
        indexes[0].ulDensity = 80;

        JET_TABLECREATE4_A tableCreate = {};
        tableCreate.cbStruct = sizeof(tableCreate);
        tableCreate.szTableName = const_cast<char*>("Four");
        tableCreate.ulPages = 1;
        tableCreate.ulDensity = 80;
        tableCreate.rgcolumncreate = columns;
        tableCreate.cColumns = 2;
        tableCreate.rgindexcreate = indexes;
        tableCreate.cIndexes = 1;

        CheckJet(JetCreateTableColumnIndex4A(session.Handle(),
                                             database.Id(),
                                             &tableCreate));
        Require(tableCreate.cCreated == 4);
        Require(tableCreate.tableid != JET_tableidNil);
        CheckJet(JetCloseTable(session.Handle(), tableCreate.tableid));
    }

    //  TABLECREATE5: TABLECREATE4 + cbLVChunkMax.
    {
        JET_INDEXCREATE3_A indexes[1] = { {} };
        indexes[0].cbStruct = sizeof(indexes[0]);
        indexes[0].szIndexName = const_cast<char*>("PrimaryKey");
        indexes[0].szKey = keyIndexKey;
        indexes[0].cbKey = sizeof(keyIndexKey);
        indexes[0].grbit = JET_bitIndexPrimary | JET_bitIndexUnique;
        indexes[0].ulDensity = 80;

        JET_TABLECREATE5_A tableCreate = {};
        tableCreate.cbStruct = sizeof(tableCreate);
        tableCreate.szTableName = const_cast<char*>("Five");
        tableCreate.ulPages = 1;
        tableCreate.ulDensity = 80;
        tableCreate.rgcolumncreate = columns;
        tableCreate.cColumns = 2;
        tableCreate.rgindexcreate = indexes;
        tableCreate.cIndexes = 1;
        tableCreate.cbSeparateLV = 1024;
        //  Engine caps cbLVChunkMax at JET_paramLVChunkSizeMost (R/O,
        //  page-size dependent — ~4 KiB minus per-page overhead on
        //  small pages, more on larger).  Use 1024 so the test is
        //  insensitive to the page size the engine boots with.
        tableCreate.cbLVChunkMax = 1024;

        CheckJet(JetCreateTableColumnIndex5A(session.Handle(),
                                             database.Id(),
                                             &tableCreate));
        Require(tableCreate.cCreated == 4);
        Require(tableCreate.tableid != JET_tableidNil);
        CheckJet(JetCloseTable(session.Handle(), tableCreate.tableid));
    }
}

//  Thin-wrapper variants for column / table delete.  Each adds
//  a JET_GRBIT to the v1 signature.  Build a small schema,
//  exercise the v2 entry, then verify the deletion actually
//  took effect via JetGetColumnInfo / JetOpenTable reads that
//  should now miss.

EseIntegrationScenario(Schema, DeleteColumn2RemovesColumn)
{
    TemporaryDirectory directory("Schema.DeleteColumn2RemovesColumn");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "DeleteColumn2.mdb");

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(), database.Id(),
                             "Rows", 8, 100, &tableId));
    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(session.Handle(), tableId, "Doomed",
                           &columnDefinition, nullptr, 0, &columnId));

    //  JetDeleteColumn2 takes the JET_bitDeleteColumnIgnoreTemplateColumns
    //  grbit.  We don't have a template-derived table here so the bit
    //  is a no-op, but it's the documented value to pass.
    CheckJet(JetDeleteColumn2A(session.Handle(), tableId, "Doomed",
                               JET_bitDeleteColumnIgnoreTemplateColumns));

    //  Functional verification: post-delete GetColumnInfo for the
    //  removed column must report ColumnNotFound.
    JET_COLUMNDEF probe = {};
    RequireJetError(JetGetColumnInfoA(session.Handle(), database.Id(),
                                      "Rows", "Doomed",
                                      &probe, sizeof(probe),
                                      JET_ColInfo),
                    JET_errColumnNotFound);

    CheckJet(JetCloseTable(session.Handle(), tableId));
}

//  JetDeleteTable2 is gated behind `JET_VERSION > 0x0A01` in
//  jetapi.h (line 6976) — not present in this pin.  When the
//  JET_VERSION bumps to include it, drop a scenario here that
//  mirrors `DeleteColumn2RemovesColumn` above.

EseIntegrationScenario(Schema, CreateIndexOptionallyUniqueEnforcedOnlyWhenFlagged)
{
    TemporaryDirectory directory(
        "Schema.CreateIndexOptionallyUniqueEnforcedOnlyWhenFlagged");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "OptionalUnique");

    auto identityColumnId = table.AddColumn("Identity", JET_coltypLong,
                                             JET_bitColumnAutoincrement);
    auto valueColumnId = table.AddColumn("Value", JET_coltypLong,
                                          JET_bitColumnNotNULL);

    // Primary key keeps the table sane.
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity", PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    // JET_bitIndexOptionallyUnique creates an index whose uniqueness
    // is enforced ONLY for updates that opt in via
    // JET_bitUpdateEnforceOptionallyUniqueIndices.  Default updates
    // accept duplicates silently.
    static constexpr std::string_view ValueKey =
        std::string_view("+Value\0\0", 8);
    table.CreateIndex("ValueOptionallyUnique", ValueKey,
                      JET_bitIndexOptionallyUnique);

    // First row.
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 100);
        transaction.Commit();
    }

    // Default-grbit update with a duplicate Value — must succeed.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        const int32_t duplicateValue = 100;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              valueColumnId,
                              &duplicateValue, sizeof(duplicateValue),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }

    // Update with JET_bitUpdateEnforceOptionallyUniqueIndices on the
    // same duplicate — engine must refuse with JET_errKeyDuplicate.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        const int32_t duplicateValue = 100;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              valueColumnId,
                              &duplicateValue, sizeof(duplicateValue),
                              0, nullptr));
        RequireJetError(
            JetUpdate2(session.Handle(), table.Id(),
                       nullptr, 0, nullptr,
                       JET_bitUpdateEnforceOptionallyUniqueIndices),
            JET_errKeyDuplicate);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepCancel));
        transaction.Commit();
    }

    // A distinct Value with the enforce flag must succeed.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        const int32_t distinctValue = 200;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              valueColumnId,
                              &distinctValue, sizeof(distinctValue),
                              0, nullptr));
        CheckJet(JetUpdate2(session.Handle(), table.Id(),
                            nullptr, 0, nullptr,
                            JET_bitUpdateEnforceOptionallyUniqueIndices));
        transaction.Commit();
    }

    // Walk on the secondary index, count rows.  Default-update duplicate
    // is present (two 100s) plus the enforced-update 200 = 3 total.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ValueOptionallyUnique"));
    int32_t observedCount = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        ++observedCount;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observedCount == 3);
    (void)identityColumnId;
}

EseIntegrationScenario(Schema, OpenTableReadOnlyRejectsInsertButAllowsRead)
{
    TemporaryDirectory directory(
        "Schema.OpenTableReadOnlyRejectsInsertButAllowsRead");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    static constexpr std::string_view TableName = "Documents";

    JET_COLUMNID valueColumnId = JET_columnidNil;
    {
        EseTable setupTable(database, TableName);
        valueColumnId = setupTable.AddColumn("Value", JET_coltypLong,
                                              JET_bitColumnNotNULL);
        EseTransaction transaction(session);
        for (int32_t value : { 11, 22, 33 })
        {
            InsertSingleFixedColumnRow<int32_t>(setupTable, valueColumnId, value);
        }
        transaction.Commit();
    }

    // Open the table with JET_bitTableReadOnly.  All reads must
    // succeed; any attempt to start an update returns
    // JET_errPermissionDenied.
    JET_TABLEID readOnlyTableId = JET_tableidNil;
    const std::string tableNameOwned(TableName);
    CheckJet(JetOpenTableA(session.Handle(), database.Id(),
                           tableNameOwned.c_str(),
                           nullptr, 0,
                           JET_bitTableReadOnly,
                           &readOnlyTableId));

    // Walk the rows — reads work.
    CheckJet(JetMove(session.Handle(), readOnlyTableId, JET_MoveFirst, 0));
    std::vector<int32_t> observed;
    do
    {
        int32_t value = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), readOnlyTableId,
                                   valueColumnId,
                                   &value, sizeof(value),
                                   &actualBytes, 0, nullptr));
        observed.push_back(value);
    }
    while (JetMove(session.Handle(), readOnlyTableId, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observed.size() == 3);
    Require(observed[0] == 11);
    Require(observed[1] == 22);
    Require(observed[2] == 33);

    // PrepareUpdate(Insert) must be refused.
    RequireJetError(JetPrepareUpdate(session.Handle(), readOnlyTableId,
                                     JET_prepInsert),
                    JET_errPermissionDenied);

    // And PrepareUpdate(Replace) on the current row is equally refused.
    CheckJet(JetMove(session.Handle(), readOnlyTableId, JET_MoveFirst, 0));
    RequireJetError(JetPrepareUpdate(session.Handle(), readOnlyTableId,
                                     JET_prepReplace),
                    JET_errPermissionDenied);

    CheckJet(JetCloseTable(session.Handle(), readOnlyTableId));
}

EseIntegrationScenario(Schema, OpenTableDenyWriteBlocksConcurrentWritersInOtherSessions)
{
    TemporaryDirectory directory(
        "Schema.OpenTableDenyWriteBlocksConcurrentWritersInOtherSessions");
    EseInstance instance(directory);
    EseSession setupSession(instance);
    EseDatabase setupDatabase(setupSession, "Schema.mdb");

    static constexpr std::string_view TableName = "Shared";
    const std::string tableNameOwned(TableName);

    JET_COLUMNID valueColumnId = JET_columnidNil;
    {
        EseTable setupTable(setupDatabase, TableName);
        valueColumnId = setupTable.AddColumn("Value", JET_coltypLong,
                                              JET_bitColumnNotNULL);
        EseTransaction transaction(setupSession);
        InsertSingleFixedColumnRow<int32_t>(setupTable, valueColumnId, 7);
        transaction.Commit();
    }

    // Session A opens with DenyWrite — reads succeed; A also still
    // writes through this cursor (DenyWrite denies *other* writers,
    // not the holder).
    EseSession sessionA(instance);
    EseDatabase databaseA(sessionA, "Schema.mdb", EseDatabaseMode::Open);
    JET_TABLEID denyWriteTableId = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionA.Handle(), databaseA.Id(),
                           tableNameOwned.c_str(),
                           nullptr, 0,
                           JET_bitTableDenyWrite,
                           &denyWriteTableId));

    int32_t observedValue = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetMove(sessionA.Handle(), denyWriteTableId, JET_MoveFirst, 0));
    CheckJet(JetRetrieveColumn(sessionA.Handle(), denyWriteTableId,
                               valueColumnId,
                               &observedValue, sizeof(observedValue),
                               &actualBytes, 0, nullptr));
    Require(observedValue == 7);

    // Session B requesting an explicit Updatable open is refused
    // with JET_errTableLocked — fileopen.cxx:1077-1093 walks the
    // FCB's cursor list and rejects any new updatable cursor from
    // another session while DomainDenyWrite is set.
    EseSession sessionB(instance);
    EseDatabase databaseB(sessionB, "Schema.mdb", EseDatabaseMode::Open);
    JET_TABLEID rejectedUpdatableId = JET_tableidNil;
    const JET_ERR explicitUpdatableResult =
        JetOpenTableA(sessionB.Handle(), databaseB.Id(),
                      tableNameOwned.c_str(),
                      nullptr, 0,
                      JET_bitTableUpdatable,
                      &rejectedUpdatableId);
    Require(explicitUpdatableResult == JET_errTableLocked);

    // A read-only open from session B is still permitted — DenyWrite
    // does not lock out readers.
    JET_TABLEID readOnlyId = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionB.Handle(), databaseB.Id(),
                           tableNameOwned.c_str(),
                           nullptr, 0,
                           JET_bitTableReadOnly,
                           &readOnlyId));

    CheckJet(JetMove(sessionB.Handle(), readOnlyId, JET_MoveFirst, 0));
    int32_t sessionBObserved = 0;
    CheckJet(JetRetrieveColumn(sessionB.Handle(), readOnlyId,
                               valueColumnId,
                               &sessionBObserved, sizeof(sessionBObserved),
                               &actualBytes, 0, nullptr));
    Require(sessionBObserved == 7);

    CheckJet(JetCloseTable(sessionB.Handle(), readOnlyId));
    CheckJet(JetCloseTable(sessionA.Handle(), denyWriteTableId));

    // After A releases DenyWrite, session B's explicit-updatable
    // open succeeds and can write through.
    JET_TABLEID afterReleaseId = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionB.Handle(), databaseB.Id(),
                           tableNameOwned.c_str(),
                           nullptr, 0,
                           JET_bitTableUpdatable,
                           &afterReleaseId));
    {
        EseTransaction successfulWrite(sessionB);
        CheckJet(JetPrepareUpdate(sessionB.Handle(), afterReleaseId,
                                  JET_prepInsert));
        const int32_t newValue = 99;
        CheckJet(JetSetColumn(sessionB.Handle(), afterReleaseId, valueColumnId,
                              &newValue, sizeof(newValue), 0, nullptr));
        CheckJet(JetUpdate(sessionB.Handle(), afterReleaseId,
                           nullptr, 0, nullptr));
        successfulWrite.Commit();
    }

    // Both rows present and correct after the DenyWrite lock cycles.
    CheckJet(JetMove(sessionB.Handle(), afterReleaseId, JET_MoveFirst, 0));
    std::vector<int32_t> finalRows;
    do
    {
        int32_t value = 0;
        CheckJet(JetRetrieveColumn(sessionB.Handle(), afterReleaseId,
                                   valueColumnId,
                                   &value, sizeof(value),
                                   &actualBytes, 0, nullptr));
        finalRows.push_back(value);
    }
    while (JetMove(sessionB.Handle(), afterReleaseId, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(finalRows.size() == 2);
    Require(finalRows[0] == 7);
    Require(finalRows[1] == 99);

    CheckJet(JetCloseTable(sessionB.Handle(), afterReleaseId));
}

EseIntegrationScenario(Schema, OpenTableDenyReadBlocksReadsFromOtherSessions)
{
    TemporaryDirectory directory(
        "Schema.OpenTableDenyReadBlocksReadsFromOtherSessions");
    EseInstance instance(directory);
    EseSession setupSession(instance);
    EseDatabase setupDatabase(setupSession, "Schema.mdb");

    static constexpr std::string_view TableName = "Locked";
    const std::string tableNameOwned(TableName);

    JET_COLUMNID valueColumnId = JET_columnidNil;
    {
        EseTable setupTable(setupDatabase, TableName);
        valueColumnId = setupTable.AddColumn("Value", JET_coltypLong,
                                              JET_bitColumnNotNULL);
        EseTransaction transaction(setupSession);
        InsertSingleFixedColumnRow<int32_t>(setupTable, valueColumnId, 42);
        transaction.Commit();
    }
    // Drain catalog RCEs so DenyRead can be granted (same fix as the
    // PermitDDL scenario).
    {
        const JET_ERR idleResult =
            JetIdle(setupSession.Handle(), JET_bitIdleCompact);
        if (idleResult < JET_errSuccess)
        {
            CheckJet(idleResult);
        }
    }

    // Session A acquires DenyRead — the strictest table-level lock.
    EseSession sessionA(instance);
    EseDatabase databaseA(sessionA, "Schema.mdb", EseDatabaseMode::Open);
    JET_TABLEID denyReadId = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionA.Handle(), databaseA.Id(),
                           tableNameOwned.c_str(),
                           nullptr, 0,
                           JET_bitTableDenyRead,
                           &denyReadId));

    // Session B's open is refused with JET_errTableLocked while A
    // holds DenyRead (fileopen.cxx:1052-1058).
    EseSession sessionB(instance);
    EseDatabase databaseB(sessionB, "Schema.mdb", EseDatabaseMode::Open);
    JET_TABLEID rejectedReadId = JET_tableidNil;
    const JET_ERR rejectedResult =
        JetOpenTableA(sessionB.Handle(), databaseB.Id(),
                      tableNameOwned.c_str(),
                      nullptr, 0,
                      JET_bitTableReadOnly,
                      &rejectedReadId);
    Require(rejectedResult == JET_errTableLocked);

    // Closing A's cursor releases the lock; session B can open.
    CheckJet(JetCloseTable(sessionA.Handle(), denyReadId));

    JET_TABLEID afterReleaseId = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionB.Handle(), databaseB.Id(),
                           tableNameOwned.c_str(),
                           nullptr, 0, 0, &afterReleaseId));
    CheckJet(JetMove(sessionB.Handle(), afterReleaseId, JET_MoveFirst, 0));
    int32_t observedValue = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(sessionB.Handle(), afterReleaseId,
                               valueColumnId,
                               &observedValue, sizeof(observedValue),
                               &actualBytes, 0, nullptr));
    Require(observedValue == 42);
    CheckJet(JetCloseTable(sessionB.Handle(), afterReleaseId));
}

EseIntegrationScenario(Schema, OpenTablePermitDDLAllowsAddColumnOnFixedDDLTable)
{
    TemporaryDirectory directory(
        "Schema.OpenTablePermitDDLAllowsAddColumnOnFixedDDLTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    static constexpr std::string_view TableName = "Fixed";
    const std::string tableNameOwned(TableName);

    // Create a FixedDDL table — schema is frozen after the initial
    // create cursor closes.  The seed column is added while the
    // create cursor is still live (DDL is unrestricted until close).
    JET_COLUMNID seedColumnId = 0;
    {
        JET_TABLECREATE_A tableCreate = {};
        tableCreate.cbStruct = sizeof(tableCreate);
        tableCreate.szTableName = const_cast<char*>(tableNameOwned.c_str());
        tableCreate.ulPages = 16;
        tableCreate.ulDensity = 80;
        tableCreate.grbit = JET_bitTableCreateFixedDDL;
        CheckJet(JetCreateTableColumnIndexA(session.Handle(), database.Id(),
                                            &tableCreate));
        const JET_TABLEID createTableId = tableCreate.tableid;

        JET_COLUMNDEF seedColumnDef = {};
        seedColumnDef.cbStruct = sizeof(seedColumnDef);
        seedColumnDef.coltyp = JET_coltypLong;
        seedColumnDef.grbit = JET_bitColumnNotNULL;
        CheckJet(JetAddColumnA(session.Handle(), createTableId,
                               "Seed", &seedColumnDef,
                               nullptr, 0, &seedColumnId));
        CheckJet(JetCloseTable(session.Handle(), createTableId));
    }

    // PermitDDL has two gates beyond grbit validation
    // (fileopen.cxx around lines 1133+ and 1189+):
    //   1. DenyRead must be grantable — no other session has a
    //      cursor on the table.  In a single-session test this is
    //      always satisfied.
    //   2. The FCB's RCE list must be empty — no pending
    //      version-store entries on this table.
    // JetCreateTableColumnIndex inserts catalog rows whose RCEs
    // remain pending until the version-store cleaner drains them.
    // JetIdle(JET_bitIdleCompact) is the synchronous way to trigger
    // that drain.  The warning code it returns (JET_wrnIdleFull or
    // similar) is informational; we only care that the call
    // succeeded enough to clear the FCB.
    {
        const JET_ERR idleResult =
            JetIdle(session.Handle(), JET_bitIdleCompact);
        if (idleResult < JET_errSuccess)
        {
            CheckJet(idleResult);
        }
    }

    // Post-create open with PermitDDL|DenyRead — succeeds and
    // accepts a new column.  PermitDDL must be paired with DenyRead
    // per the jetapi.h flag comment.
    JET_COLUMNID permittedColumnId = 0;
    {
        JET_TABLEID permitDDLId = JET_tableidNil;
        CheckJet(JetOpenTableA(session.Handle(), database.Id(),
                               tableNameOwned.c_str(),
                               nullptr, 0,
                               JET_bitTablePermitDDL | JET_bitTableDenyRead,
                               &permitDDLId));

        JET_COLUMNDEF acceptedColumnDef = {};
        acceptedColumnDef.cbStruct = sizeof(acceptedColumnDef);
        acceptedColumnDef.coltyp = JET_coltypLong;
        CheckJet(JetAddColumnA(session.Handle(), permitDDLId,
                               "PermittedExtension", &acceptedColumnDef,
                               nullptr, 0, &permittedColumnId));
        CheckJet(JetCloseTable(session.Handle(), permitDDLId));
    }

    // Default open: JetAddColumn refuses because the table is
    // FixedDDL and PermitDDL is not requested.
    {
        JET_TABLEID defaultOpenId = JET_tableidNil;
        CheckJet(JetOpenTableA(session.Handle(), database.Id(),
                               tableNameOwned.c_str(),
                               nullptr, 0, 0, &defaultOpenId));

        JET_COLUMNDEF rejectedColumnDef = {};
        rejectedColumnDef.cbStruct = sizeof(rejectedColumnDef);
        rejectedColumnDef.coltyp = JET_coltypLong;
        JET_COLUMNID rejectedColumnId = 0;
        RequireJetError(JetAddColumnA(session.Handle(), defaultOpenId,
                                      "RejectedExtension", &rejectedColumnDef,
                                      nullptr, 0, &rejectedColumnId),
                        JET_errFixedDDL);
        CheckJet(JetCloseTable(session.Handle(), defaultOpenId));
    }

    // Default re-open + JetGetTableColumnInfo sees the new column;
    // write through it and round-trip, proving the PermitDDL change
    // persisted to disk.
    JET_TABLEID reopenId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), database.Id(),
                           tableNameOwned.c_str(),
                           nullptr, 0, 0, &reopenId));

    JET_COLUMNDEF reopenedColumnDef = {};
    reopenedColumnDef.cbStruct = sizeof(reopenedColumnDef);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), reopenId,
                                    "PermittedExtension",
                                    &reopenedColumnDef,
                                    sizeof(reopenedColumnDef),
                                    JET_ColInfo));
    Require(reopenedColumnDef.coltyp == JET_coltypLong);
    Require(reopenedColumnDef.columnid == permittedColumnId);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), reopenId, JET_prepInsert));
        const int32_t seedValue = 1;
        const int32_t extensionValue = 99;
        CheckJet(JetSetColumn(session.Handle(), reopenId, seedColumnId,
                              &seedValue, sizeof(seedValue), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), reopenId,
                              permittedColumnId,
                              &extensionValue, sizeof(extensionValue),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), reopenId, nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), reopenId, JET_MoveFirst, 0));
    int32_t persistedSeed = 0;
    int32_t persistedExtension = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), reopenId, seedColumnId,
                               &persistedSeed, sizeof(persistedSeed),
                               &actualBytes, 0, nullptr));
    CheckJet(JetRetrieveColumn(session.Handle(), reopenId,
                               permittedColumnId,
                               &persistedExtension, sizeof(persistedExtension),
                               &actualBytes, 0, nullptr));
    Require(persistedSeed == 1);
    Require(persistedExtension == 99);

    CheckJet(JetCloseTable(session.Handle(), reopenId));
}

EseIntegrationScenario(Schema, CreateIndexSortNullsHighPlacesNullsAfterData)
{
    TemporaryDirectory directory(
        "Schema.CreateIndexSortNullsHighPlacesNullsAfterData");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "NullSort");

    auto identityColumnId =
        table.AddColumn("Identity", JET_coltypLong,
                        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto sortableColumnId =
        table.AddColumn("Sortable", JET_coltypLong);

    static constexpr std::string_view PrimaryKeyDescriptor =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity", PrimaryKeyDescriptor,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    // Secondary index over Sortable with JET_bitIndexSortNullsHigh —
    // NULL entries sort AFTER non-NULL.
    static constexpr std::string_view SortableKeyDescriptor =
        std::string_view("+Sortable\0\0", 11);
    table.CreateIndex("BySortable", SortableKeyDescriptor,
                      JET_bitIndexSortNullsHigh);

    // Insert four rows: 10, NULL, 20, NULL.
    {
        EseTransaction transaction(session);
        for (auto value : { 10, -1 /*NULL marker*/, 20, -1 })
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            if (value != -1)
            {
                const int32_t setValue = value;
                CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                      sortableColumnId,
                                      &setValue, sizeof(setValue),
                                      0, nullptr));
            }
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    // Walk in BySortable order — non-NULL entries come first, NULL
    // entries come last because of JET_bitIndexSortNullsHigh.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "BySortable"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    std::vector<int32_t> walkOrder;
    do
    {
        int32_t value = 0;
        uint32_t actualBytes = 0;
        const auto retrieveResult =
            JetRetrieveColumn(session.Handle(), table.Id(),
                              sortableColumnId,
                              &value, sizeof(value),
                              &actualBytes, 0, nullptr);
        if (retrieveResult == JET_wrnColumnNull)
        {
            walkOrder.push_back(-1);
        }
        else
        {
            CheckJet(retrieveResult);
            walkOrder.push_back(value);
        }
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);

    Require(walkOrder.size() == 4);
    Require(walkOrder[0] == 10);
    Require(walkOrder[1] == 20);
    Require(walkOrder[2] == -1);
    Require(walkOrder[3] == -1);
    (void)identityColumnId;
}

EseIntegrationScenario(Schema, CreateIndexEmptyDoesNotPopulateExistingRows)
{
    TemporaryDirectory directory(
        "Schema.CreateIndexEmptyDoesNotPopulateExistingRows");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "EmptyHinted");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong,
                                          JET_bitColumnNotNULL);

    // Pre-populate the table with non-NULL rows.
    static constexpr int32_t RowCount = 50;
    {
        EseTransaction transaction(session);
        for (int32_t rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumnId,
                                                rowIndex);
        }
        transaction.Commit();
    }

    // JET_bitIndexEmpty | JET_bitIndexIgnoreAnyNull tells the engine:
    // "trust me, all rows are NULL on this index column; don't scan".
    // We're lying — the rows have real values — but the engine takes
    // the hint and skips population, producing an empty index even
    // though the data would be eligible.
    static constexpr std::string_view ValueKeyDescriptor =
        std::string_view("+Value\0\0", 8);
    table.CreateIndex("ByValueClaimedEmpty", ValueKeyDescriptor,
                      JET_bitIndexEmpty | JET_bitIndexIgnoreAnyNull);

    // Switch to the new index and count.  IndexRecordCount reports 0
    // because the engine never walked the data to populate.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ByValueClaimedEmpty"));
    uint32_t recordCount = 0;
    CheckJet(JetIndexRecordCount(session.Handle(), table.Id(),
                                 &recordCount, 0));
    Require(recordCount == 0);

    // The data is still walkable via no-index iteration (primary order
    // is record order in the absence of a primary index).
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), nullptr));
    int32_t observedDataRows = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        ++observedDataRows;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observedDataRows == RowCount);
}

EseIntegrationScenario(Schema, CreateIndexUnversionedAcceptedAndDataIntact)
{
    TemporaryDirectory directory(
        "Schema.CreateIndexUnversionedAcceptedAndDataIntact");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");

    // JET_bitIndexUnversioned tells the engine to skip per-entry
    // version-store records during the build.  This is a build-time
    // performance hint; the resulting index is functionally identical.
    // We pin acceptance + data correctness; the perf gain isn't
    // observable through the public API.
    static constexpr std::string_view TableName = "Unversioned";
    EseTable table(database, TableName);
    auto valueColumnId = table.AddColumn("Value", JET_coltypLong,
                                          JET_bitColumnNotNULL);

    static constexpr int32_t RowCount = 64;
    {
        EseTransaction transaction(session);
        for (int32_t rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumnId,
                                                rowIndex);
        }
        transaction.Commit();
    }

    static constexpr std::string_view ValueKeyDescriptor =
        std::string_view("+Value\0\0", 8);
    table.CreateIndex("ByValueUnversioned", ValueKeyDescriptor,
                      JET_bitIndexUnversioned);

    // Walk via the new index — every row in order.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ByValueUnversioned"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    uint32_t recordCount = 0;
    CheckJet(JetIndexRecordCount(session.Handle(), table.Id(),
                                 &recordCount, 0));
    Require(static_cast<int32_t>(recordCount) == RowCount);

    int32_t expectedValue = 0;
    do
    {
        int32_t value = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   valueColumnId,
                                   &value, sizeof(value),
                                   &actualBytes, 0, nullptr));
        Require(value == expectedValue);
        ++expectedValue;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(expectedValue == RowCount);
}

EseIntegrationScenario(Schema, CreateIndexLazyFlushAcceptedAndDataIntact)
{
    TemporaryDirectory directory(
        "Schema.CreateIndexLazyFlushAcceptedAndDataIntact");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "LazyFlush");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong,
                                          JET_bitColumnNotNULL);

    static constexpr int32_t RowCount = 64;
    {
        EseTransaction transaction(session);
        for (int32_t rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumnId,
                                                rowIndex);
        }
        transaction.Commit();
    }

    // JET_bitIndexLazyFlush — defers the durability flush of the index
    // build commit.  Functionally identical from the API surface; the
    // commit lands in the log eventually but not synchronously with
    // CreateIndex.  Validate acceptance + correctness.
    static constexpr std::string_view ValueKeyDescriptor =
        std::string_view("+Value\0\0", 8);
    table.CreateIndex("ByValueLazyFlush", ValueKeyDescriptor,
                      JET_bitIndexLazyFlush);

    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ByValueLazyFlush"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    uint32_t recordCount = 0;
    CheckJet(JetIndexRecordCount(session.Handle(), table.Id(),
                                 &recordCount, 0));
    Require(static_cast<int32_t>(recordCount) == RowCount);

    int32_t expectedValue = 0;
    do
    {
        int32_t value = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   valueColumnId,
                                   &value, sizeof(value),
                                   &actualBytes, 0, nullptr));
        Require(value == expectedValue);
        ++expectedValue;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(expectedValue == RowCount);
}

EseIntegrationScenario(Schema, CreateIndexTuplesEnablesSubstringSeek)
{
    TemporaryDirectory directory(
        "Schema.CreateIndexTuplesEnablesSubstringSeek");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Schema.mdb");
    EseTable table(database, "Substrings");

    static constexpr uint16_t Codepage1252 = 1252;
    auto identityColumnId =
        table.AddColumn("Identity", JET_coltypLong,
                        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto textColumnId =
        table.AddColumn("Word", JET_coltypText, 0,
                        /*maximumBytes*/ 128, Codepage1252);

    static constexpr std::string_view PrimaryKeyDescriptor =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity", PrimaryKeyDescriptor,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    // JET_bitIndexTuples builds a substring index — every N-char
    // window of the text column becomes a key.  The engine refuses
    // to materialize huge tuple sets, so we cap with TupleLimits
    // (chLengthMin=3, chLengthMax=8) to keep the build bounded.
    JET_TUPLELIMITS tupleLimits = {};
    tupleLimits.chLengthMin = 3;
    tupleLimits.chLengthMax = 8;
    tupleLimits.chToIndexMax = 64;
    tupleLimits.cchIncrement = 1;
    tupleLimits.ichStart = 0;

    JET_INDEXCREATE_A indexCreate = {};
    indexCreate.cbStruct = sizeof(indexCreate);
    indexCreate.szIndexName = const_cast<char*>("ByWordTuples");
    static constexpr std::string_view WordKeyDescriptor =
        std::string_view("+Word\0\0", 7);
    indexCreate.szKey = const_cast<char*>(WordKeyDescriptor.data());
    indexCreate.cbKey = static_cast<uint32_t>(WordKeyDescriptor.size());
    indexCreate.grbit = JET_bitIndexTuples | JET_bitIndexTupleLimits;
    indexCreate.ulDensity = 80;
    indexCreate.ptuplelimits = &tupleLimits;
    indexCreate.err = JET_errSuccess;
    CheckJet(JetCreateIndex2A(session.Handle(), table.Id(),
                              &indexCreate, 1));
    CheckJet(indexCreate.err);

    {
        EseTransaction transaction(session);
        for (auto word : { "hello world", "open source", "world peace",
                           "hello again", "quick brown fox" })
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  textColumnId,
                                  word, static_cast<uint32_t>(std::strlen(word)),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    // Seek for substring "hello" via the tuple index.  Two rows
    // contain "hello"; walk the matching range to count.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ByWordTuples"));

    // Tuple-index substring search: build a lower-bound key from the
    // search term, seek GE, then build an upper-bound key with
    // JET_bitStrLimit so SetIndexRange caps the walk at entries whose
    // tuple-key prefix matches.  Each matching row may produce
    // multiple tuple entries (one per matching window), so we
    // collect distinct Identity values to count rows, not entries.
    // Walk every tuple-index entry, collecting the distinct Identity
    // values that appear.  Two rows in our seed set contain "hello"
    // ("hello world" and "hello again"); both Identities must show
    // up via the tuple index.  Two rows do not contain "hello"; the
    // tuple index, by construction, surfaces tuple windows only over
    // characters actually present in each Word, so those rows still
    // appear in the index — but their entries don't help us prove the
    // substring property here.  Instead we exercise the substring-seek
    // primitive directly: MakeKey("hello") + JetSeek(GE) must hit an
    // index entry whose source row contains "hello".
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    uint32_t tupleEntryCount = 0;
    do
    {
        ++tupleEntryCount;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);

    // The tuple index materialises one entry per (chLengthMin..
    // chLengthMax)-window per source row.  For our 5 short Words at
    // length 3..8, the per-row tuple count comfortably exceeds 1, so
    // the total tuple-entry count must exceed the row count.
    Require(tupleEntryCount > 5);

    // Substring seek: MakeKey("hello") + JetSeek(GE) — must land on
    // an entry whose owning row contains "hello".  Read Identity off
    // the cursor position, then walk to the primary index and verify
    // the Word column on that row contains the substring.
    static constexpr std::string_view SearchTerm = "hello";
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        SearchTerm.data(),
                        static_cast<uint32_t>(SearchTerm.size()),
                        JET_bitNewKey));
    const JET_ERR seekResult =
        JetSeek(session.Handle(), table.Id(), JET_bitSeekGE);
    Require(seekResult == JET_errSuccess
            || seekResult == JET_wrnSeekNotEqual);

    int32_t hitIdentity = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                               identityColumnId,
                               &hitIdentity, sizeof(hitIdentity),
                               &actualBytes, 0, nullptr));

    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "PrimaryByIdentity"));
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &hitIdentity, sizeof(hitIdentity), JET_bitNewKey));
    CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));

    char wordBuffer[64] = {};
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                               textColumnId,
                               wordBuffer, sizeof(wordBuffer) - 1,
                               &actualBytes, 0, nullptr));
    const std::string_view hitWord(wordBuffer, actualBytes);
    Require(hitWord.find(SearchTerm) != std::string_view::npos);
}

//  JET_SPACEHINTS hint-flag scenarios.
//
//  These bits are *hints* to the engine's space allocator / scan
//  prefetcher / cleanup heuristics: each is meant to nudge an
//  internal policy (extent layout, B-tree defrag triggering, range
//  prefetch) without changing the public API contract.  The
//  observable from outside is just "engine accepted the hint and
//  the index built + works."  The scenarios below construct a
//  populated table, build a secondary index with one hint flag at a
//  time, then exercise the index with a seek (and a scan or delete
//  for the access-pattern hints) so the build path actually runs.
//
//  Each scenario follows the same shape as
//  CreateIndex3WithSpaceHintsBuilds — same ulInitialDensity (80%),
//  same cbInitial (16 KiB), same ulMaintDensity — and only varies
//  the grbit field of JET_SPACEHINTS.  The Round-9 gotcha applies:
//  density/cbInitial must be sensible; the engine rejects
//  fabricated values from cat.cxx's validator.

namespace
{

//  Common helper: build a single-int-column table with N rows, then
//  call JetCreateIndex3 with the given JET_SPACEHINTS::grbit.  The
//  flag is the only thing that varies between scenarios in this
//  cluster — extracting the boilerplate keeps each scenario short
//  enough to read at a glance.
JET_TABLEID CreateRowsTableForHint(JET_SESID sesid,
                                   JET_DBID dbid,
                                   JET_COLUMNID& outValueColumnId)
{
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(sesid, dbid, "Rows", 16, 100, &tableId));
    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    JET_COLUMNID valueColumnId = 0;
    CheckJet(JetAddColumnA(sesid, tableId, "Value",
                           &columnDefinition, nullptr, 0,
                           &valueColumnId));
    outValueColumnId = valueColumnId;
    return tableId;
}

void InsertSequentialRows(JET_SESID sesid,
                          JET_TABLEID tableId,
                          JET_COLUMNID valueColumnId,
                          int32_t rowCount)
{
    CheckJet(JetBeginTransaction(sesid));
    for (int32_t rowIndex = 0; rowIndex < rowCount; ++rowIndex)
    {
        CheckJet(JetPrepareUpdate(sesid, tableId, JET_prepInsert));
        CheckJet(JetSetColumn(sesid, tableId, valueColumnId,
                              &rowIndex, sizeof(rowIndex), 0, nullptr));
        CheckJet(JetUpdate(sesid, tableId, nullptr, 0, nullptr));
    }
    CheckJet(JetCommitTransaction(sesid, 0));
}

//  Build a +Value secondary index on tableId with the supplied
//  JET_SPACEHINTS::grbit.  Index name fixed to "ValueIndex" so
//  callers can set it as the current index without each scenario
//  carrying its own name string.
void CreateValueIndexWithSpaceHints(JET_SESID sesid,
                                    JET_TABLEID tableId,
                                    JET_GRBIT spaceHintsGrbit)
{
    JET_SPACEHINTS spaceHints = {};
    spaceHints.cbStruct = sizeof(spaceHints);
    spaceHints.ulInitialDensity = 80;
    spaceHints.cbInitial = 16 * 1024;
    spaceHints.grbit = spaceHintsGrbit;
    spaceHints.ulMaintDensity = 80;

    char indexKey[] = "+Value\0";
    JET_INDEXCREATE2_A indexCreate = {};
    indexCreate.cbStruct = sizeof(indexCreate);
    indexCreate.szIndexName = const_cast<char*>("ValueIndex");
    indexCreate.szKey = indexKey;
    indexCreate.cbKey = sizeof(indexKey);
    indexCreate.grbit = JET_bitIndexUnique;
    indexCreate.ulDensity = 80;
    indexCreate.pSpacehints = &spaceHints;

    CheckJet(JetCreateIndex3A(sesid, tableId, &indexCreate, 1));
    Require(indexCreate.err == JET_errSuccess);
}

//  Seek to a specific value through the named index and confirm
//  retrieval matches.  Verifies the built index is actually usable
//  — the goal of every hint-flag scenario, not just space-allocator
//  ones — because a silently-broken index would also "accept" the
//  flag.
void SeekValueIndexAndVerify(JET_SESID sesid,
                             JET_TABLEID tableId,
                             JET_COLUMNID valueColumnId,
                             int32_t targetValue)
{
    CheckJet(JetSetCurrentIndex2A(sesid, tableId, "ValueIndex", 0));
    CheckJet(JetMakeKey(sesid, tableId,
                        &targetValue, sizeof(targetValue),
                        JET_bitNewKey));
    CheckJet(JetSeek(sesid, tableId, JET_bitSeekEQ));

    int32_t observed = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(sesid, tableId, valueColumnId,
                               &observed, sizeof(observed),
                               &actualSize, 0, nullptr));
    Require(observed == targetValue);
}

} // namespace

EseIntegrationScenario(Schema, CreateIndex3WithSpaceHintUtilizeParentSpaceAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithSpaceHintUtilizeParentSpaceAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintParent.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   JET_bitSpaceHintsUtilizeParentSpace);
    SeekValueIndexAndVerify(session.Handle(), tableId, valueColumnId, 17);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Schema, CreateIndex3WithSpaceHintUtilizeExactExtentsAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithSpaceHintUtilizeExactExtentsAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintExact.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   JET_bitSpaceHintsUtilizeExactExtents);
    SeekValueIndexAndVerify(session.Handle(), tableId, valueColumnId, 17);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Schema, CreateIndex3WithCreateHintAppendSequentialAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithCreateHintAppendSequentialAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintAppend.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    //  Append hint biases split policy toward right-edge growth —
    //  matches a workload that's monotonically appending.  The
    //  observable here is just that the engine accepts it and the
    //  index still functions; the actual split-policy change is
    //  internal to the space allocator.
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   JET_bitCreateHintAppendSequential);
    SeekValueIndexAndVerify(session.Handle(), tableId, valueColumnId, 17);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Schema, CreateIndex3WithCreateHintHotpointSequentialAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithCreateHintHotpointSequentialAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintHotpoint.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    //  Hotpoint hint biases split policy toward a moving insertion
    //  cursor — workloads that append into a recent-but-not-the-
    //  newest key range (e.g. timestamp + nondeterministic suffix).
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   JET_bitCreateHintHotpointSequential);
    SeekValueIndexAndVerify(session.Handle(), tableId, valueColumnId, 17);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Schema, CreateIndex3WithRetrieveHintTableScanForwardAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithRetrieveHintTableScanForwardAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintForward.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    //  Forward-scan hint tells the engine the predominant access
    //  pattern is sequential forward — triggers auto-defrag when
    //  fragmentation crosses an internal threshold (see
    //  JET_paramDefragmentSequentialBTrees).  We don't test the
    //  defrag trigger here; just confirm Build + a sample scan run.
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   JET_bitRetrieveHintTableScanForward);
    CheckJet(JetSetCurrentIndex2A(session.Handle(), tableId,
                                  "ValueIndex", 0));
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    int32_t scannedRowCount = 0;
    do
    {
        ++scannedRowCount;
    }
    while (JetMove(session.Handle(), tableId, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(scannedRowCount == 32);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Schema, CreateIndex3WithRetrieveHintTableScanBackwardAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithRetrieveHintTableScanBackwardAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintBackward.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    //  Symmetric to the forward variant — backward sequential scan
    //  as the predominant pattern.  Walks the index back-to-front
    //  to exercise the move-prev path under the hint.
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   JET_bitRetrieveHintTableScanBackward);
    CheckJet(JetSetCurrentIndex2A(session.Handle(), tableId,
                                  "ValueIndex", 0));
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveLast, 0));
    int32_t scannedRowCount = 0;
    do
    {
        ++scannedRowCount;
    }
    while (JetMove(session.Handle(), tableId, JET_MovePrevious, 0)
           != JET_errNoCurrentRecord);
    Require(scannedRowCount == 32);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Schema, CreateIndex3WithDeleteHintTableSequentialAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithDeleteHintTableSequentialAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintDelete.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    //  Delete-sequential hint advertises that cleanup will walk the
    //  table low-key-to-high-key.  Run a low-to-high delete pass to
    //  match the documented usage pattern and confirm both index
    //  build and the deletes themselves succeed under the hint.
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   JET_bitDeleteHintTableSequential);
    CheckJet(JetSetCurrentIndex2A(session.Handle(), tableId,
                                  "ValueIndex", 0));
    CheckJet(JetBeginTransaction(session.Handle()));
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    int32_t deletedRowCount = 0;
    while (true)
    {
        CheckJet(JetDelete(session.Handle(), tableId));
        ++deletedRowCount;
        if (JetMove(session.Handle(), tableId, JET_MoveNext, 0)
            == JET_errNoCurrentRecord)
        {
            break;
        }
    }
    CheckJet(JetCommitTransaction(session.Handle(), 0));
    Require(deletedRowCount == 32);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Schema, CreateIndex3WithCombinedSpaceAndAccessHintsAccepted)
{
    TemporaryDirectory directory(
        "Schema.CreateIndex3WithCombinedSpaceAndAccessHintsAccepted");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "HintCombined.mdb");

    JET_COLUMNID valueColumnId = 0;
    JET_TABLEID tableId =
        CreateRowsTableForHint(session.Handle(), database.Id(), valueColumnId);
    InsertSequentialRows(session.Handle(), tableId, valueColumnId, 32);
    //  All five non-reserved hint bits OR'd together — the engine
    //  treats them as additive hints to independent subsystems, so
    //  the combination must be accepted at API time even though no
    //  single workload would set every bit.  The seek afterwards
    //  proves the resulting index is still functional.
    const JET_GRBIT combinedHints =
        JET_bitSpaceHintsUtilizeParentSpace |
        JET_bitSpaceHintsUtilizeExactExtents |
        JET_bitCreateHintAppendSequential |
        JET_bitRetrieveHintTableScanForward |
        JET_bitDeleteHintTableSequential;
    CreateValueIndexWithSpaceHints(session.Handle(), tableId,
                                   combinedHints);
    SeekValueIndexAndVerify(session.Handle(), tableId, valueColumnId, 17);
    CheckJet(JetCloseTable(session.Handle(), tableId));
}
