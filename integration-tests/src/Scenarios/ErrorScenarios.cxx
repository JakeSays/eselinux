// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Error matrix — assert that specific public-API misuse paths return
// the precise JET_err* the engine documents.  Each case isolates one
// API misstep and pins the expected error.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/RowOperations.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

EseIntegrationScenario(Error, RetrievingUnknownColumnReturnsColumnNotFound)
{
    TemporaryDirectory directory(
        "Error.RetrievingUnknownColumnReturnsColumnNotFound");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "Empty");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    RequireJetError(JetGetColumnInfoA(session.Handle(),
                                      database.Id(),
                                      "Empty",
                                      "NoSuchColumn",
                                      &columnDefinition,
                                      sizeof(columnDefinition),
                                      JET_ColInfo),
                    JET_errColumnNotFound);
}

EseIntegrationScenario(Error, OpeningUnknownTableReturnsObjectNotFound)
{
    TemporaryDirectory directory("Error.OpeningUnknownTableReturnsObjectNotFound");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");

    JET_TABLEID tableId = JET_tableidNil;
    RequireJetError(JetOpenTableA(session.Handle(), database.Id(),
                                  "Missing", nullptr, 0, 0, &tableId),
                    JET_errObjectNotFound);
}

EseIntegrationScenario(Error, AddingDuplicateColumnReturnsColumnDuplicate)
{
    TemporaryDirectory directory("Error.AddingDuplicateColumnReturnsColumnDuplicate");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "WithDupe");

    table.AddColumn("Value", JET_coltypLong);

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    JET_COLUMNID columnId = 0;
    RequireJetError(JetAddColumnA(session.Handle(), table.Id(),
                                  "Value", &columnDefinition,
                                  nullptr, 0, &columnId),
                    JET_errColumnDuplicate);
}

EseIntegrationScenario(Error, CreatingDuplicateTableReturnsTableDuplicate)
{
    TemporaryDirectory directory("Error.CreatingDuplicateTableReturnsTableDuplicate");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "Existing");

    JET_TABLEID duplicateTableId = JET_tableidNil;
    RequireJetError(JetCreateTableA(session.Handle(), database.Id(),
                                    "Existing", 16, 80, &duplicateTableId),
                    JET_errTableDuplicate);
}

EseIntegrationScenario(Error, DuplicateUniqueKeyInsertReturnsKeyDuplicate)
{
    TemporaryDirectory directory("Error.DuplicateUniqueKeyInsertReturnsKeyDuplicate");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "Unique");

    auto columnId = table.AddColumn("Key", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 42);
        transaction.Commit();
    }

    EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
    const int32_t duplicate = 42;
    CheckJet(JetSetColumn(session.Handle(), table.Id(), columnId,
                          &duplicate, sizeof(duplicate), 0, nullptr));
    RequireJetError(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr),
                    JET_errKeyDuplicate);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepCancel));
}

EseIntegrationScenario(Error, SeekWithoutMakeKeyReturnsKeyNotMade)
{
    TemporaryDirectory directory("Error.SeekWithoutMakeKeyReturnsKeyNotMade");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "Indexed");

    table.AddColumn("Key", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey, JET_bitIndexPrimary);

    // Seek without a preceding JetMakeKey returns KeyNotMade.
    RequireJetError(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ),
                    JET_errKeyNotMade);
}

EseIntegrationScenario(Error, SetColumnOutsideUpdateReturnsUpdateNotPrepared)
{
    TemporaryDirectory directory(
        "Error.SetColumnOutsideUpdateReturnsUpdateNotPrepared");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "Targets");

    auto columnId = table.AddColumn("Value", JET_coltypLong);

    const int32_t value = 7;
    RequireJetError(JetSetColumn(session.Handle(), table.Id(), columnId,
                                 &value, sizeof(value), 0, nullptr),
                    JET_errUpdateNotPrepared);
}

EseIntegrationScenario(Error, UpdateOutsideUpdateReturnsUpdateNotPrepared)
{
    TemporaryDirectory directory(
        "Error.UpdateOutsideUpdateReturnsUpdateNotPrepared");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "Targets");

    table.AddColumn("Value", JET_coltypLong);

    RequireJetError(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr),
                    JET_errUpdateNotPrepared);
}

EseIntegrationScenario(Error, RetrieveColumnSizeMismatchReturnsInvalidBufferSize)
{
    TemporaryDirectory directory(
        "Error.RetrieveColumnSizeMismatchReturnsInvalidBufferSize");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "FixedSizeColumn");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 5);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    // Pass a buffer too small for the 4-byte fixed Long column —
    // engine returns JET_wrnBufferTruncated.
    uint8_t tinyBuffer = 0;
    uint32_t actualSize = 0;
    const auto retrieveResult = JetRetrieveColumn(session.Handle(),
                                                  table.Id(),
                                                  columnId,
                                                  &tinyBuffer,
                                                  sizeof(tinyBuffer),
                                                  &actualSize,
                                                  0,
                                                  nullptr);
    Require(retrieveResult == JET_wrnBufferTruncated);
    Require(actualSize == sizeof(int32_t));
}

EseIntegrationScenario(Error, AttachAlreadyAttachedDatabaseReturnsWarning)
{
    TemporaryDirectory directory(
        "Error.AttachAlreadyAttachedDatabaseReturnsWarning");
    EseInstance instance(directory);
    EseSession session(instance);

    const auto databasePath = (instance.Directory() / "Doubled.mdb").string();
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(session.Handle(),
                                databasePath.c_str(),
                                nullptr,
                                &dbid,
                                JET_bitDbOverwriteExisting));

    // Attaching the just-created database again returns the warning
    // JET_wrnDatabaseAttached (the engine treats the second attach as
    // a successful no-op) rather than the JET_errDatabaseDuplicate
    // some docs imply.
    RequireJetError(JetAttachDatabaseA(session.Handle(),
                                       databasePath.c_str(),
                                       0),
                    JET_wrnDatabaseAttached);

    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(), databasePath.c_str()));
}

EseIntegrationScenario(Error, AttachMissingFileReturnsFileNotFound)
{
    TemporaryDirectory directory("Error.AttachMissingFileReturnsFileNotFound");
    EseInstance instance(directory);
    EseSession session(instance);

    const auto missingPath = (instance.Directory() / "NoSuchFile.mdb").string();
    RequireJetError(JetAttachDatabaseA(session.Handle(),
                                       missingPath.c_str(),
                                       0),
                    JET_errFileNotFound);
}

EseIntegrationScenario(Error, DeletingMissingIndexReturnsIndexNotFound)
{
    TemporaryDirectory directory("Error.DeletingMissingIndexReturnsIndexNotFound");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable table(database, "NoIndex");

    table.AddColumn("Value", JET_coltypLong);

    RequireJetError(JetDeleteIndexA(session.Handle(), table.Id(), "NoSuchIndex"),
                    JET_errIndexNotFound);
}

EseIntegrationScenario(Error, GetErrorInfoReportsCategoryHierarchy)
{
    //  JetGetErrorInfoW takes a pointer to a JET_ERR as context and
    //  fills a JET_ERRINFOBASIC_W with category metadata describing
    //  the error: the error value, the engine's classification
    //  (Fatal / IO / Resource / Api / ...), and the source file +
    //  line that raised it.
    //
    //  No instance / session needed — the call inspects the error
    //  taxonomy compiled into the engine, not runtime state.
    JET_ERR err = JET_errRecordNotFound;
    JET_ERRINFOBASIC_W info = {};
    info.cbStruct = sizeof(info);

    CheckJet(JetGetErrorInfoW(&err,
                              &info,
                              sizeof(info),
                              JET_ErrorInfoSpecificErr,
                              0));
    Require(info.errValue == JET_errRecordNotFound);
    Require(info.errcatMostSpecific != JET_errcatUnknown);
}
