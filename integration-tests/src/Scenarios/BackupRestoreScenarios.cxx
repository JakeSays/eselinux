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

// TODO Phase 5: full backup-restore round-trip via JetBackupInstance →
// fresh JetInstance → JetRestoreInstance → reattach → walk every row.
// The restore-side plumbing needs a second instance handle in a fresh
// directory and matching system-path params, which is straightforward
// but adds enough machinery to be worth its own focused pass.
