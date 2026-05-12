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

#include <filesystem>
#include <system_error>

using namespace ese::tests;

EseIntegrationScenario(BackupRestore, StreamingBackupProducesNonEmptyDirectory)
{
    TemporaryDirectory directory("BackupRestore.StreamingBackupProducesNonEmptyDirectory");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Backup.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < 100; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    const auto backupDirectory = directory.Path() / "backup";
    std::error_code errorCode;
    std::filesystem::create_directories(backupDirectory, errorCode);
    Require(!errorCode);

    CheckJet(JetBackupInstanceA(instance.Handle(),
                                backupDirectory.string().c_str(),
                                0,
                                nullptr));

    // Backup must have produced at least one file under the target.
    int backupArtifactCount = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(backupDirectory, errorCode))
    {
        if (entry.is_regular_file())
        {
            ++backupArtifactCount;
        }
    }
    Require(!errorCode);
    Require(backupArtifactCount > 0);
}

EseIntegrationScenario(BackupRestore, ExternalBackupExposesAttachInfo)
{
    TemporaryDirectory directory("BackupRestore.ExternalBackupExposesAttachInfo");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "External.mdb");
    EseTable table(database, "Rows");

    table.AddColumn("Value", JET_coltypLong);

    // JetBeginExternalBackupInstance starts an external backup
    // session.  JetGetAttachInfoInstanceA lists the database files
    // included in the backup as a multi-string buffer (filename\0
    // filename\0…\0).  We don't copy anything — just verify the
    // enumeration succeeds and reports at least one attached database.
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));

    char     attachInfoBuffer[4096] = {};
    uint32_t attachInfoActualBytes  = 0;
    CheckJet(JetGetAttachInfoInstanceA(instance.Handle(),
                                       attachInfoBuffer,
                                       sizeof(attachInfoBuffer),
                                       &attachInfoActualBytes));
    Require(attachInfoActualBytes > 0);

    CheckJet(JetEndExternalBackupInstance(instance.Handle()));
}

EseIntegrationScenario(BackupRestore, FullBackupRestoreRoundTrip)
{
    TemporaryDirectory sourceDirectory("BackupRestore.FullRoundTrip.source");
    TemporaryDirectory restoreDirectory("BackupRestore.FullRoundTrip.restore");

    const auto backupDirectory = sourceDirectory.Path() / "backup";

    static constexpr int RowCount = 200;

    // Phase A: build a populated database in sourceDirectory and back
    // it up to backupDirectory, then JetTerm the source instance.
    {
        EseInstance sourceInstance(sourceDirectory);
        EseSession session(sourceInstance);
        EseDatabase database(session, "Original.mdb");
        EseTable table(database, "Rows");

        auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();

        std::error_code errorCode;
        std::filesystem::create_directories(backupDirectory, errorCode);
        Require(!errorCode);

        CheckJet(JetBackupInstanceA(sourceInstance.Handle(),
                                    backupDirectory.string().c_str(),
                                    0,
                                    nullptr));
    }

    // Phase B: JetRestoreInstanceA takes the instance by value
    // (jetapi.cxx around line 22128) and won't return an allocated
    // handle to the caller — so we have to allocate the instance
    // ourselves via JetCreateInstance2A first, then set params, then
    // restore.  After restore the instance is uninitialised
    // (jetapi.cxx around line 22212); a regular JetInit follows.
    JET_INSTANCE restoreHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&restoreHandle,
                                 "ese-tests-restore",
                                 "ese-tests-restore",
                                 0));

    auto restorePathWithSeparator = restoreDirectory.Path().string();
    if (!restorePathWithSeparator.empty() &&
        restorePathWithSeparator.back() != '/')
    {
        restorePathWithSeparator.push_back('/');
    }

    CheckJet(JetSetSystemParameterA(&restoreHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    restorePathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&restoreHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    restorePathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&restoreHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    restorePathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&restoreHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&restoreHandle, JET_sesidNil,
                                    JET_paramEventSource, 0, "ese-tests-restore"));
    CheckJet(JetSetSystemParameterA(&restoreHandle, JET_sesidNil,
                                    JET_paramCircularLog, 1, nullptr));

    // szDest=nullptr would restore to the database's original path
    // (source dir).  We want it in restoreDirectory.
    CheckJet(JetRestoreInstanceA(restoreHandle,
                                 backupDirectory.string().c_str(),
                                 restorePathWithSeparator.c_str(),
                                 nullptr));

    CheckJet(JetInit(&restoreHandle));

    JET_SESID restoreSession = JET_sesidNil;
    CheckJet(JetBeginSessionA(restoreHandle, &restoreSession, nullptr, nullptr));

    const auto restoredDatabasePath =
        (restoreDirectory.Path() / "Original.mdb").string();
    CheckJet(JetAttachDatabaseA(restoreSession,
                                restoredDatabasePath.c_str(),
                                0));
    JET_DBID restoredDbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(restoreSession,
                              restoredDatabasePath.c_str(),
                              nullptr, &restoredDbid, 0));

    JET_TABLEID restoredTableId = JET_tableidNil;
    CheckJet(JetOpenTableA(restoreSession, restoredDbid, "Rows",
                           nullptr, 0, 0, &restoredTableId));

    int observed = 0;
    CheckJet(JetMove(restoreSession, restoredTableId, JET_MoveFirst, 0));
    do
    {
        ++observed;
    }
    while (JetMove(restoreSession, restoredTableId, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observed == RowCount);

    CheckJet(JetCloseTable(restoreSession, restoredTableId));
    CheckJet(JetCloseDatabase(restoreSession, restoredDbid, 0));
    CheckJet(JetDetachDatabaseA(restoreSession, restoredDatabasePath.c_str()));
    CheckJet(JetEndSession(restoreSession, 0));
    CheckJet(JetTerm(restoreHandle));
}
