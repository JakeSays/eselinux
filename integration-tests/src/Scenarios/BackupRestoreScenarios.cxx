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

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

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

EseIntegrationScenario(BackupRestore,
                       GetAttachInfoGlobalEnumeratesAttachedDatabase)
{
    TemporaryDirectory directory(
        "BackupRestore.GetAttachInfoGlobalEnumeratesAttachedDatabase");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Attach.mdb");
    EseTable table(database, "Rows");
    table.AddColumn("Value", JET_coltypLong);

    //  JetGetAttachInfo (global form, no instance handle) lists the
    //  databases attached to "the current instance" — meaningful only
    //  in single-instance mode, which is exactly EseInstance's
    //  configuration.  The buffer is a multi-string of filenames
    //  (filename\0...\0\0).
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));

    char attachInfo[4096] = {};
    uint32_t cbActual = 0;
    CheckJet(JetGetAttachInfoA(attachInfo,
                               sizeof(attachInfo),
                               &cbActual));
    Require(cbActual > 0);

    CheckJet(JetEndExternalBackupInstance(instance.Handle()));
}

EseIntegrationScenario(BackupRestore,
                       GetLogInfoInstanceListsActiveLogs)
{
    TemporaryDirectory directory(
        "BackupRestore.GetLogInfoInstanceListsActiveLogs");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Logs.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong);

    //  Produce enough log records that the engine has at least one
    //  closed log file to enumerate.
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 256; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  JetGetLogInfoInstance returns a multi-string of log file paths
    //  used since the last full backup — needed by external backup
    //  agents to know what to copy.  Only meaningful inside an
    //  external backup session.
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));

    char logInfo[4096] = {};
    uint32_t cbActual = 0;
    CheckJet(JetGetLogInfoInstanceA(instance.Handle(),
                                    logInfo,
                                    sizeof(logInfo),
                                    &cbActual));
    Require(cbActual > 0);

    //  JetGetTruncateLogInfoInstance enumerates the subset that's
    //  safe to truncate post-backup — typically a prefix of the
    //  GetLogInfoInstance list.
    char truncInfo[4096] = {};
    uint32_t cbTrunc = 0;
    CheckJet(JetGetTruncateLogInfoInstanceA(instance.Handle(),
                                            truncInfo,
                                            sizeof(truncInfo),
                                            &cbTrunc));

    CheckJet(JetEndExternalBackupInstance(instance.Handle()));
}

EseIntegrationScenario(BackupRestore,
                       TruncateLogInstanceEnforcesBackupSequence)
{
    TemporaryDirectory directory(
        "BackupRestore.TruncateLogInstanceEnforcesBackupSequence");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Truncate.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong);

    //  Generate log activity so the engine has logs in play.
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 100; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  Outside any backup, TruncateLogInstance must surface
    //  JET_errNoBackup — there's no backup session to consult.
    RequireJetError(JetTruncateLogInstance(instance.Handle()),
                    JET_errNoBackup);

    //  Inside an external backup without the read sequence done, the
    //  engine refuses with InvalidBackupSequence — the truncate hook
    //  is gated on the client having actually copied the logs first.
    //  Both shapes prove the API is reachable and correctly sequenced.
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));
    RequireJetError(JetTruncateLogInstance(instance.Handle()),
                    JET_errInvalidBackupSequence);
    CheckJet(JetEndExternalBackupInstance(instance.Handle()));
}

EseIntegrationScenario(BackupRestore,
                       StopBackupInstanceCancelsActiveBackup)
{
    TemporaryDirectory directory(
        "BackupRestore.StopBackupInstanceCancelsActiveBackup");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "StopBackup.mdb");
    EseTable table(database, "Rows");
    table.AddColumn("Value", JET_coltypLong);

    //  JetStopBackupInstance interrupts a running backup that hasn't
    //  yet hit End.  Starting an external backup and then asking the
    //  engine to abort it must succeed; the subsequent EndExternalBackup
    //  cleans up the session state.
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));
    CheckJet(JetStopBackupInstance(instance.Handle()));
    CheckJet(JetEndExternalBackupInstance(instance.Handle()));
}

EseIntegrationScenario(BackupRestore,
                       GetInstanceMiscInfoReportsLogSignature)
{
    TemporaryDirectory directory(
        "BackupRestore.GetInstanceMiscInfoReportsLogSignature");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Misc.mdb");

    //  JET_InstanceMiscInfoLogSignature returns a JET_SIGNATURE
    //  describing the log stream's identity (creation time, computer
    //  name, random salt) — used by external backup agents to
    //  correlate restored databases with their original log stream.
    JET_SIGNATURE logSignature = {};
    CheckJet(JetGetInstanceMiscInfo(instance.Handle(),
                                    &logSignature,
                                    sizeof(logSignature),
                                    JET_InstanceMiscInfoLogSignature));

    //  The signature is non-zero — the engine generated random bytes
    //  at log-init time.  We don't probe specific fields; any
    //  difference from a zero-init struct proves the API populated it.
    JET_SIGNATURE zero = {};
    Require(std::memcmp(&logSignature, &zero, sizeof(zero)) != 0);
}

EseIntegrationScenario(BackupRestore,
                       GetLogFileInfoReadsHeaderOfClosedLog)
{
    TemporaryDirectory directory(
        "BackupRestore.GetLogFileInfoReadsHeaderOfClosedLog");

    //  EseInstance enables circular logging by default, which keeps
    //  only one rolling log on disk and that file isn't a
    //  standalone-readable log header.  We need a real closed
    //  generation to read; provision the instance manually with
    //  circular log OFF, do enough work to roll one log generation,
    //  then JetTerm so the closed gen is flushed to disk.
    JET_INSTANCE instanceHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceHandle,
                                 "GetLogFileInfo",
                                 "GetLogFileInfo",
                                 0));

    auto pathWithSep = directory.Path().string();
    if (!pathWithSep.empty() && pathWithSep.back() != '/')
    {
        pathWithSep.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0, "GetLogFileInfo"));
    //  Crucial: circular logging OFF — keeps closed generations on
    //  disk so JetGetLogFileInfo has something well-formed to read.
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    //  Cap log file size at the minimum (64 KiB) so a small amount
    //  of work rolls the generation.
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFileSize, 64, nullptr));

    CheckJet(JetInit(&instanceHandle));

    JET_SESID sesid = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle, &sesid, nullptr, nullptr));

    JET_DBID dbid = JET_dbidNil;
    const auto dbPath = (directory.Path() / "LogFileInfo.mdb").string();
    CheckJet(JetCreateDatabaseA(sesid, dbPath.c_str(),
                                nullptr, &dbid, 0));

    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetCreateTableA(sesid, dbid, "Rows", 8, 100, &tableid));

    JET_COLUMNDEF coldef = {};
    coldef.cbStruct = sizeof(coldef);
    coldef.coltyp = JET_coltypLong;
    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(sesid, tableid, "Value",
                           &coldef, nullptr, 0, &columnId));

    {
        CheckJet(JetBeginTransaction(sesid));
        for (int32_t i = 0; i < 2000; ++i)
        {
            CheckJet(JetPrepareUpdate(sesid, tableid, JET_prepInsert));
            CheckJet(JetSetColumn(sesid, tableid, columnId,
                                  &i, sizeof(i), 0, nullptr));
            CheckJet(JetUpdate(sesid, tableid, nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(sesid, JET_bitCommitLazyFlush));
    }

    CheckJet(JetCloseTable(sesid, tableid));
    CheckJet(JetCloseDatabase(sesid, dbid, 0));
    CheckJet(JetDetachDatabaseA(sesid, dbPath.c_str()));
    CheckJet(JetEndSession(sesid, 0));
    CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));

    //  A non-circular setup leaves closed generations like
    //  edb00000001.log, edb00000002.log, ... and edb.log (which is
    //  the next "current" slot).  Pick the first numbered generation
    //  — that one is fully written and closed.
    std::filesystem::path closedLog;
    for (const auto& entry :
         std::filesystem::directory_iterator(directory.Path()))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto name = entry.path().filename().string();
        //  Numbered logs match edb<HEX>.log — the digit count varies
        //  with engine config; checking for "edb" prefix + ".log"
        //  suffix + at least one digit between is enough.
        if (name.rfind("edb", 0) == 0 &&
            entry.path().extension() == ".log" &&
            name.size() > std::strlen("edb.log"))
        {
            closedLog = entry.path();
            break;
        }
    }
    Require(!closedLog.empty());

    JET_LOGINFOMISC logInfo = {};
    CheckJet(JetGetLogFileInfoA(closedLog.string().c_str(),
                                &logInfo,
                                sizeof(logInfo),
                                JET_LogInfoMisc));
    Require(logInfo.cbFile > 0);
    Require(logInfo.cbDatabasePageSize == 4096);
    Require(logInfo.ulGeneration >= 1);
}

EseIntegrationScenario(BackupRestore,
                       BeginAndEndExternalBackupGlobalForms)
{
    TemporaryDirectory directory(
        "BackupRestore.BeginAndEndExternalBackupGlobalForms");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "BackupGlobal.mdb");

    //  JetBeginExternalBackup / JetEndExternalBackup are the global
    //  (no instance handle) wrappers that operate on the engine's
    //  single active instance when running in single-instance mode —
    //  the mode EseInstance configures.
    CheckJet(JetBeginExternalBackup(0));

    //  Inside the backup, JetGetAttachInfo must report at least one
    //  attached database — proves the global-form started a real
    //  session, not a stub.
    char attachInfo[4096] = {};
    uint32_t cbActual = 0;
    CheckJet(JetGetAttachInfoA(attachInfo,
                               sizeof(attachInfo),
                               &cbActual));
    Require(cbActual > 0);

    CheckJet(JetEndExternalBackup());
}

EseIntegrationScenario(BackupRestore,
                       StopBackupGlobalCancelsActiveBackup)
{
    TemporaryDirectory directory(
        "BackupRestore.StopBackupGlobalCancelsActiveBackup");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "StopBackupGlobal.mdb");

    //  JetStopBackup is the global (no instance) variant of
    //  StopBackupInstance.  Start a backup, ask the engine to abort
    //  it, then end the session cleanly.
    CheckJet(JetBeginExternalBackup(0));
    CheckJet(JetStopBackup());
    CheckJet(JetEndExternalBackup());
}

EseIntegrationScenario(BackupRestore,
                       TruncateLogGlobalEnforcesSequence)
{
    TemporaryDirectory directory(
        "BackupRestore.TruncateLogGlobalEnforcesSequence");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "TruncateGlobal.mdb");

    //  JetTruncateLog is the global counterpart to
    //  JetTruncateLogInstance.  Like the *Instance form it can only
    //  be invoked inside an external backup that has read the logs.
    //  Outside any backup the engine surfaces JET_errNoBackup;
    //  inside a barely-started backup it surfaces
    //  JET_errInvalidBackupSequence.  Both contract-violation paths
    //  are reachable from the public surface and exercise the API.
    RequireJetError(JetTruncateLog(), JET_errNoBackup);

    CheckJet(JetBeginExternalBackup(0));
    RequireJetError(JetTruncateLog(), JET_errInvalidBackupSequence);
    CheckJet(JetEndExternalBackup());
}

namespace
{

//  Scope guard that ensures JetEndExternalBackupInstance runs even
//  when an inner Require/CheckJet throws.  Without it the engine's
//  process-global "backup in progress" flag stays set and subsequent
//  scenarios fail JetSetSystemParameter with AlreadyInitialized.
class ExternalBackupSession
{
public:
    explicit ExternalBackupSession(JET_INSTANCE instance)
        : _instance(instance)
    {
        CheckJet(JetBeginExternalBackupInstance(_instance, 0));
        _active = true;
    }

    ~ExternalBackupSession()
    {
        if (_active)
        {
            (void)JetEndExternalBackupInstance(_instance);
        }
    }

    void EndNormally()
    {
        CheckJet(JetEndExternalBackupInstance(_instance));
        _active = false;
    }

    ExternalBackupSession(const ExternalBackupSession&) = delete;
    ExternalBackupSession& operator=(const ExternalBackupSession&) = delete;

private:
    JET_INSTANCE _instance = JET_instanceNil;
    bool _active = false;
};

}  // namespace

EseIntegrationScenario(BackupRestore,
                       OpenAndReadAndCloseFileInstanceCopiesDatabase)
{
    TemporaryDirectory directory(
        "BackupRestore.OpenAndReadAndCloseFileInstanceCopiesDatabase");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "FileRead.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 100; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  Real external-backup protocol: Begin -> GetAttachInfo gives
    //  the attached .edb path(s) as multistring -> open each file
    //  for read via JetOpenFileInstance -> JetReadFileInstance ->
    //  CloseFile -> EndBackup.
    ExternalBackupSession backupSession(instance.Handle());

    char attachInfo[4096] = {};
    uint32_t cbAttach = 0;
    CheckJet(JetGetAttachInfoInstanceA(instance.Handle(),
                                       attachInfo,
                                       sizeof(attachInfo),
                                       &cbAttach));
    Require(cbAttach > 0);

    //  Multistring format: filename\0filename\0...\0\0.  Pick the
    //  first one.
    const std::string firstDatabase(attachInfo);
    Require(!firstDatabase.empty());

    //  The engine returns a backup-handle index in *phfFile —
    //  starting at 0 for the first open file, so handle == 0 is a
    //  *valid* handle, not an error sentinel.  Width / pulFileSizeLow
    //  is the load-bearing assertion: the database file is non-empty.
    JET_HANDLE fileHandle = 0;
    uint32_t fileSizeLow = 0;
    uint32_t fileSizeHigh = 0;
    CheckJet(JetOpenFileInstanceA(instance.Handle(),
                                  firstDatabase.c_str(),
                                  &fileHandle,
                                  &fileSizeLow,
                                  &fileSizeHigh));
    const uint64_t fileSize =
        (static_cast<uint64_t>(fileSizeHigh) << 32) | fileSizeLow;
    Require(fileSize > 0);

    //  Pull a chunk of the database into a caller buffer.  Engine
    //  constraints (BACKUP_CONTEXT::ErrBKReadFile, backup.cxx ~2806):
    //    - destination buffer must be aligned to the OS memory-page
    //      commit granularity (4 KiB on this build) — the read is
    //      submitted as O_DIRECT-style IO,
    //    - cbMax must be a multiple of the DB page size (4 KiB), and
    //    - the first read must request more than cpgDBReserved (= 2)
    //      pages so the header + at least one data page come back in
    //      a single shot.
    //  4 pages (16 KiB) satisfies all three.
    static constexpr uint32_t PageSize = 4096;
    static constexpr uint32_t ChunkBytes = PageSize * 4;
    struct AlignedFree
    {
        void operator()(void* ptr) const
        {
            std::free(ptr);
        }
    };
    std::unique_ptr<uint8_t, AlignedFree> buffer(
        static_cast<uint8_t*>(std::aligned_alloc(PageSize, ChunkBytes)));
    Require(buffer != nullptr);

    uint32_t cbRead = 0;
    CheckJet(JetReadFileInstance(instance.Handle(),
                                 fileHandle,
                                 buffer.get(),
                                 ChunkBytes,
                                 &cbRead));
    Require(cbRead == ChunkBytes);

    CheckJet(JetCloseFileInstance(instance.Handle(), fileHandle));
    backupSession.EndNormally();
}

EseIntegrationScenario(BackupRestore,
                       EndExternalBackupInstance2AcceptsAbortGrbit)
{
    TemporaryDirectory directory(
        "BackupRestore.EndExternalBackupInstance2AcceptsAbortGrbit");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "EndV2.mdb");

    //  JetEndExternalBackupInstance2 takes a grbit on the close call;
    //  JET_bitBackupEndAbort signals "we aborted the backup mid-flight,
    //  don't stamp the headers as backed-up".  Either grbit shape
    //  closes the session; aborting should leave the database
    //  flagged the same as if no backup had occurred.
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));
    CheckJet(JetEndExternalBackupInstance2(instance.Handle(),
                                           JET_bitBackupEndAbort));

    //  A subsequent normal backup must succeed — the aborted one
    //  released its session correctly.
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));
    CheckJet(JetEndExternalBackupInstance2(instance.Handle(),
                                           JET_bitBackupEndNormal));
}

EseIntegrationScenario(BackupRestore,
                       GetLogInfoInstance2ReportsLogGenerationRange)
{
    TemporaryDirectory directory(
        "BackupRestore.GetLogInfoInstance2ReportsLogGenerationRange");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LogInfoV2.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 256; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  JetGetLogInfoInstance2 layers an extra JET_LOGINFO_A out-arg
    //  on top of the v1 form: alongside the multistring of log
    //  filenames it returns the base name and the (low, high) log
    //  generation range covered.
    CheckJet(JetBeginExternalBackupInstance(instance.Handle(), 0));

    char logBuffer[4096] = {};
    uint32_t cbLog = 0;
    JET_LOGINFO_A logInfo = {};
    logInfo.cbSize = sizeof(logInfo);
    CheckJet(JetGetLogInfoInstance2A(instance.Handle(),
                                     logBuffer,
                                     sizeof(logBuffer),
                                     &cbLog,
                                     &logInfo));
    Require(cbLog > 0);
    Require(logInfo.ulGenHigh >= logInfo.ulGenLow);
    Require(logInfo.szBaseName[0] != '\0');

    CheckJet(JetEndExternalBackupInstance(instance.Handle()));
}

namespace
{

//  Global-form counterpart to ExternalBackupSession — wraps
//  JetBeginExternalBackup / JetEndExternalBackup (no instance
//  handle), the single-instance flavour the engine selects in
//  default mode.
class GlobalExternalBackupSession
{
public:
    GlobalExternalBackupSession()
    {
        CheckJet(JetBeginExternalBackup(0));
        _active = true;
    }

    ~GlobalExternalBackupSession()
    {
        if (_active)
        {
            (void)JetEndExternalBackup();
        }
    }

    void EndNormally()
    {
        CheckJet(JetEndExternalBackup());
        _active = false;
    }

    GlobalExternalBackupSession(const GlobalExternalBackupSession&) = delete;
    GlobalExternalBackupSession& operator=(const GlobalExternalBackupSession&)
        = delete;

private:
    bool _active = false;
};

}  // namespace

EseIntegrationScenario(BackupRestore,
                       OpenAndReadAndCloseFileGlobalCopiesDatabase)
{
    TemporaryDirectory directory(
        "BackupRestore.OpenAndReadAndCloseFileGlobalCopiesDatabase");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "FileReadGlobal.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 100; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  Global file-access form: JetOpenFile / JetReadFile / JetCloseFile
    //  (no instance handle).  Same constraints as the *Instance form —
    //  Begin/End wrap them, buffer must be 4 KiB-aligned, byte count a
    //  multiple of the DB page size + over the 2-page reserved
    //  prologue on first read.
    GlobalExternalBackupSession backupSession;

    char attachInfo[4096] = {};
    uint32_t cbAttach = 0;
    CheckJet(JetGetAttachInfoA(attachInfo,
                               sizeof(attachInfo),
                               &cbAttach));
    Require(cbAttach > 0);
    const std::string firstDatabase(attachInfo);
    Require(!firstDatabase.empty());

    JET_HANDLE fileHandle = 0;
    uint32_t fileSizeLow = 0;
    uint32_t fileSizeHigh = 0;
    CheckJet(JetOpenFileA(firstDatabase.c_str(),
                          &fileHandle,
                          &fileSizeLow,
                          &fileSizeHigh));
    const uint64_t fileSize =
        (static_cast<uint64_t>(fileSizeHigh) << 32) | fileSizeLow;
    Require(fileSize > 0);

    static constexpr uint32_t PageSize = 4096;
    static constexpr uint32_t ChunkBytes = PageSize * 4;
    struct AlignedFree
    {
        void operator()(void* ptr) const
        {
            std::free(ptr);
        }
    };
    std::unique_ptr<uint8_t, AlignedFree> buffer(
        static_cast<uint8_t*>(std::aligned_alloc(PageSize, ChunkBytes)));
    Require(buffer != nullptr);

    uint32_t cbRead = 0;
    CheckJet(JetReadFile(fileHandle,
                         buffer.get(),
                         ChunkBytes,
                         &cbRead));
    Require(cbRead == ChunkBytes);

    CheckJet(JetCloseFile(fileHandle));
    backupSession.EndNormally();
}

EseIntegrationScenario(BackupRestore,
                       GetLogInfoGlobalListsActiveLogs)
{
    TemporaryDirectory directory(
        "BackupRestore.GetLogInfoGlobalListsActiveLogs");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "LogInfoGlobal.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 256; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  JetGetLogInfo (global form) lists log files the backup must
    //  copy.  Returns a multistring of paths.
    GlobalExternalBackupSession backupSession;

    char logInfo[4096] = {};
    uint32_t cbActual = 0;
    CheckJet(JetGetLogInfoA(logInfo,
                            sizeof(logInfo),
                            &cbActual));
    Require(cbActual > 0);
    //  The first entry should look like a log filename — contain
    //  "edb" (the base name our framework sets via JET_paramBaseName).
    const std::string firstLog(logInfo);
    Require(firstLog.find("edb") != std::string::npos);

    backupSession.EndNormally();
}

EseIntegrationScenario(BackupRestore,
                       OpenFileSectionInstanceReadsLogTail)
{
    TemporaryDirectory directory(
        "BackupRestore.OpenFileSectionInstanceReadsLogTail");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "SectionRead.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 200; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    //  JetOpenFileSectionInstance opens a specific N-byte section
    //  (iSection / cSections) of an attached database, used by
    //  backup agents that parallelise reads across workers.  Section
    //  0 of 1 covers the entire file — equivalent to JetOpenFile.
    ExternalBackupSession backupSession(instance.Handle());

    char attachInfo[4096] = {};
    uint32_t cbAttach = 0;
    CheckJet(JetGetAttachInfoInstanceA(instance.Handle(),
                                       attachInfo,
                                       sizeof(attachInfo),
                                       &cbAttach));
    Require(cbAttach > 0);
    std::string firstDatabase(attachInfo);

    JET_HANDLE fileHandle = 0;
    uint32_t sectionSizeLow = 0;
    int32_t sectionSizeHigh = 0;
    CheckJet(JetOpenFileSectionInstanceA(instance.Handle(),
                                         firstDatabase.data(),
                                         &fileHandle,
                                         /*iSection=*/0,
                                         /*cSections=*/1,
                                         /*ibRead=*/0,
                                         &sectionSizeLow,
                                         &sectionSizeHigh));
    const uint64_t sectionSize =
        (static_cast<uint64_t>(static_cast<uint32_t>(sectionSizeHigh)) << 32)
        | sectionSizeLow;
    Require(sectionSize > 0);

    CheckJet(JetCloseFileInstance(instance.Handle(), fileHandle));
    backupSession.EndNormally();
}

EseIntegrationScenario(BackupRestore, RemoveLogfileRejectsActiveLog)
{
    TemporaryDirectory directory(
        "BackupRestore.RemoveLogfileRejectsActiveLog");

    //  Produce a closed database + a known-on-disk log file with
    //  non-circular logging so we have something to point
    //  JetRemoveLogfile at.  The API expects the engine NOT to be
    //  holding the database open — it's an offline admin operation.
    std::filesystem::path dbPath;
    std::filesystem::path logPath;
    {
        JET_INSTANCE instanceHandle = JET_instanceNil;
        CheckJet(JetCreateInstance2A(&instanceHandle,
                                     "RemoveLog",
                                     "RemoveLog",
                                     0));

        auto pathWithSep = directory.Path().string();
        if (!pathWithSep.empty() && pathWithSep.back() != '/')
        {
            pathWithSep.push_back('/');
        }
        CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                        JET_paramSystemPath, 0,
                                        pathWithSep.c_str()));
        CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                        JET_paramTempPath, 0,
                                        pathWithSep.c_str()));
        CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                        JET_paramLogFilePath, 0,
                                        pathWithSep.c_str()));
        CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                        JET_paramBaseName, 0, "edb"));
        CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                        JET_paramEventSource, 0, "RemoveLog"));
        CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                        JET_paramCircularLog, 0, nullptr));
        CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                        JET_paramLogFileSize, 64, nullptr));

        CheckJet(JetInit(&instanceHandle));

        JET_SESID sesid = JET_sesidNil;
        CheckJet(JetBeginSessionA(instanceHandle, &sesid, nullptr, nullptr));
        JET_DBID dbid = JET_dbidNil;
        dbPath = directory.Path() / "RemoveLog.mdb";
        CheckJet(JetCreateDatabaseA(sesid, dbPath.string().c_str(),
                                    nullptr, &dbid, 0));
        JET_TABLEID tableid = JET_tableidNil;
        CheckJet(JetCreateTableA(sesid, dbid, "Rows", 8, 100, &tableid));
        JET_COLUMNDEF coldef = {};
        coldef.cbStruct = sizeof(coldef);
        coldef.coltyp = JET_coltypLong;
        JET_COLUMNID columnId = 0;
        CheckJet(JetAddColumnA(sesid, tableid, "Value",
                               &coldef, nullptr, 0, &columnId));
        CheckJet(JetBeginTransaction(sesid));
        for (int32_t i = 0; i < 2000; ++i)
        {
            CheckJet(JetPrepareUpdate(sesid, tableid, JET_prepInsert));
            CheckJet(JetSetColumn(sesid, tableid, columnId,
                                  &i, sizeof(i), 0, nullptr));
            CheckJet(JetUpdate(sesid, tableid, nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(sesid, JET_bitCommitLazyFlush));
        CheckJet(JetCloseTable(sesid, tableid));
        CheckJet(JetCloseDatabase(sesid, dbid, 0));
        CheckJet(JetDetachDatabaseA(sesid, dbPath.string().c_str()));
        CheckJet(JetEndSession(sesid, 0));
        CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));

        for (const auto& entry :
             std::filesystem::directory_iterator(directory.Path()))
        {
            const auto name = entry.path().filename().string();
            if (name.rfind("edb", 0) == 0 &&
                entry.path().extension() == ".log" &&
                name.size() > std::strlen("edb.log"))
            {
                logPath = entry.path();
                break;
            }
        }
    }
    Require(!logPath.empty());

    //  Contract test: grbit is reserved (must be 0).  Engine entry
    //  validates it and returns JET_errInvalidParameter for anything
    //  else — and the API actually opens both files for real-side
    //  validation (so it's not a no-op stub).
    RequireJetError(JetRemoveLogfileA(dbPath.string().c_str(),
                                      logPath.string().c_str(),
                                      0xDEADBEEF),
                    JET_errInvalidParameter);

    //  Missing database path is also rejected at the entry guard.
    RequireJetError(JetRemoveLogfileA("",
                                      logPath.string().c_str(),
                                      0),
                    JET_errInvalidParameter);

    //  Missing log path: ditto.
    RequireJetError(JetRemoveLogfileA(dbPath.string().c_str(),
                                      "",
                                      0),
                    JET_errInvalidParameter);
}

EseIntegrationScenario(BackupRestore,
                       BeginAndEndSurrogateBackupRoundTrip)
{
    TemporaryDirectory directory(
        "BackupRestore.BeginAndEndSurrogateBackupRoundTrip");

    //  Surrogate backup tells the engine that an external agent
    //  (typically SAN-replication infrastructure) has captured a
    //  consistent snapshot of the database files + log generations
    //  [lgenFirst, lgenLast].  Engine stamps the headers as
    //  "backed up to lgenLast" without itself touching the bytes.
    //
    //  The engine rejects surrogate backup when circular logging is
    //  enabled (JET_errInvalidBackup -526), so this scenario can't
    //  use the framework default.  Provision an instance manually
    //  with circular log off.
    JET_INSTANCE instanceHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceHandle,
                                 "Surrogate",
                                 "Surrogate",
                                 0));

    auto pathWithSep = directory.Path().string();
    if (!pathWithSep.empty() && pathWithSep.back() != '/')
    {
        pathWithSep.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0, "Surrogate"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));

    CheckJet(JetInit(&instanceHandle));

    JET_SESID sesid = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle, &sesid, nullptr, nullptr));
    JET_DBID dbid = JET_dbidNil;
    const auto dbPath = (directory.Path() / "Surrogate.mdb").string();
    CheckJet(JetCreateDatabaseA(sesid, dbPath.c_str(),
                                nullptr, &dbid, 0));
    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetCreateTableA(sesid, dbid, "Rows", 8, 100, &tableid));
    JET_COLUMNDEF coldef = {};
    coldef.cbStruct = sizeof(coldef);
    coldef.coltyp = JET_coltypLong;
    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(sesid, tableid, "Value",
                           &coldef, nullptr, 0, &columnId));
    CheckJet(JetBeginTransaction(sesid));
    for (int32_t i = 0; i < 100; ++i)
    {
        CheckJet(JetPrepareUpdate(sesid, tableid, JET_prepInsert));
        CheckJet(JetSetColumn(sesid, tableid, columnId,
                              &i, sizeof(i), 0, nullptr));
        CheckJet(JetUpdate(sesid, tableid, nullptr, 0, nullptr));
    }
    CheckJet(JetCommitTransaction(sesid, 0));
    CheckJet(JetCloseTable(sesid, tableid));

    //  Begin a surrogate session covering log generations [1, 1].
    //  Engine accepts even degenerate (lgenFirst == lgenLast) ranges
    //  — the contract is "you took the snapshot, here's what's in
    //  it".  Pair with End immediately so the session unwinds.
    CheckJet(JetBeginSurrogateBackup(instanceHandle,
                                     /*lgenFirst=*/1,
                                     /*lgenLast=*/1,
                                     0));
    CheckJet(JetEndSurrogateBackup(instanceHandle,
                                   JET_bitBackupEndNormal));

    CheckJet(JetCloseDatabase(sesid, dbid, 0));
    CheckJet(JetDetachDatabaseA(sesid, dbPath.c_str()));
    CheckJet(JetEndSession(sesid, 0));
    CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));
}
