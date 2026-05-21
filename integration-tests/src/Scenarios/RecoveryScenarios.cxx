// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Recovery scenarios use CrashHelper's fork+SIGKILL machinery: the
// child opens a fresh engine, commits rows, signals ready, then waits
// to be killed. The parent SIGKILLs, then re-opens the engine on the
// same directory — JetInit replays the logs and committed rows must
// surface.

#include "Framework/Check.hxx"
#include "Framework/CrashHelper.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/RowOperations.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace ese::tests;

namespace
{

// Names must be unique across the whole binary — they map 1:1 to
// CrashHelper's child-entry table.
constexpr const char* ChildEntryCommittedRows =
    "Recovery.CommittedRowsSurviveSigkill";

// Child-side routine for the committed-rows scenario.  Opens an
// instance whose paths point at `directory`, commits 5 rows into a
// "Survivors" table, signals ready, then sleeps forever (or until
// SIGKILL'd by the parent).  Doesn't use EseInstance because we want
// to deliberately *not* JetTerm — the test exercises crash recovery.
void RunCommittedRowsChild(const std::filesystem::path& directory)
{
    JET_INSTANCE instanceHandle = JET_instanceNil;
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }

    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ese-tests-child"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 1, nullptr));

    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle, &sessionId, nullptr, nullptr));

    const auto databasePath = directory / "Survivors.mdb";
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(sessionId,
                                databasePath.string().c_str(),
                                nullptr,
                                &databaseId,
                                JET_bitDbOverwriteExisting));

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(sessionId, databaseId, "Survivors", 16, 80, &tableId));

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    columnDefinition.grbit = JET_bitColumnNotNULL;

    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(sessionId, tableId, "Value",
                           &columnDefinition, nullptr, 0, &columnId));

    CheckJet(JetBeginTransaction(sessionId));
    for (int rowIndex = 0; rowIndex < 5; ++rowIndex)
    {
        const int32_t value = 100 + rowIndex;
        CheckJet(JetPrepareUpdate(sessionId, tableId, JET_prepInsert));
        CheckJet(JetSetColumn(sessionId, tableId, columnId,
                              &value, sizeof(value), 0, nullptr));
        CheckJet(JetUpdate(sessionId, tableId, nullptr, 0, nullptr));
    }
    // Default commit (grbit == 0) waits for the log to flush durably
    // before returning, so log records are on disk by the time we
    // signal ready.
    CheckJet(JetCommitTransaction(sessionId, 0));

    ChildProcess::SignalReady(directory);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

// Register the child entry at static init so the child process (which
// re-runs static initialisers before invoking main) can find it.
struct CommittedRowsRegistrar
{
    CommittedRowsRegistrar()
    {
        RegisterChildEntry(ChildEntryCommittedRows, &RunCommittedRowsChild);
    }
};
[[maybe_unused]] static CommittedRowsRegistrar _committedRowsRegistrar;

constexpr const char* ChildEntryUncommittedRows =
    "Recovery.UncommittedRowsDiscardedAfterSigkill";

// Child for the uncommitted-rows scenario.  Opens an instance, inserts
// 5 rows inside a transaction that is *never* committed, signals
// ready, then waits to be killed.  Recovery on the parent side must
// discard the uncommitted work.
void RunUncommittedRowsChild(const std::filesystem::path& directory)
{
    JET_INSTANCE instanceHandle = JET_instanceNil;
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }

    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ese-tests-child"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 1, nullptr));

    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle, &sessionId, nullptr, nullptr));

    const auto databasePath = directory / "Discardable.mdb";
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(sessionId, databasePath.string().c_str(),
                                nullptr, &databaseId,
                                JET_bitDbOverwriteExisting));

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(sessionId, databaseId, "Pending", 16, 80, &tableId));

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    columnDefinition.grbit = JET_bitColumnNotNULL;
    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(sessionId, tableId, "Value",
                           &columnDefinition, nullptr, 0, &columnId));

    // We need at least one committed transaction so the database is
    // recoverable into a sane state; create + add column are DDL
    // operations that are auto-committed.  The pending rows we add
    // below stay in the version store and must NOT survive the SIGKILL.
    CheckJet(JetBeginTransaction(sessionId));
    for (int rowIndex = 0; rowIndex < 5; ++rowIndex)
    {
        const int32_t value = 555 + rowIndex;
        CheckJet(JetPrepareUpdate(sessionId, tableId, JET_prepInsert));
        CheckJet(JetSetColumn(sessionId, tableId, columnId,
                              &value, sizeof(value), 0, nullptr));
        CheckJet(JetUpdate(sessionId, tableId, nullptr, 0, nullptr));
    }
    // Deliberately do NOT commit.  Signal ready and wait for SIGKILL.

    ChildProcess::SignalReady(directory);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

struct UncommittedRowsRegistrar
{
    UncommittedRowsRegistrar()
    {
        RegisterChildEntry(ChildEntryUncommittedRows, &RunUncommittedRowsChild);
    }
};
[[maybe_unused]] static UncommittedRowsRegistrar _uncommittedRowsRegistrar;

// =====================================================================
// Recovery grbit children — share the same JetInit-bootstrap and
// instance-param setup with the rows-survive scenarios above, but
// commit into TWO databases (so the parent can delete one to test
// JET_bitReplayIgnoreMissingDB) and disable circular logging so the
// log stream accumulates across rotations (needed for
// JET_bitReplayIgnoreLostLogs).
// =====================================================================

constexpr const char* ChildEntryTwoDatabases =
    "Recovery.TwoDatabasesCrashedBeforeTerm";

void RunTwoDatabasesChild(const std::filesystem::path& directory)
{
    JET_INSTANCE instanceHandle = JET_instanceNil;
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }

    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ese-tests-child"));
    // Circular log OFF — old generations accumulate in the directory
    // so the parent can delete a specific generation.
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));

    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle, &sessionId, nullptr, nullptr));

    auto buildDatabase = [&](const char* fileName, int32_t startingValue)
    {
        const auto path = directory / fileName;
        JET_DBID databaseId = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(sessionId,
                                    path.string().c_str(),
                                    nullptr, &databaseId,
                                    JET_bitDbOverwriteExisting));
        JET_TABLEID tableId = JET_tableidNil;
        CheckJet(JetCreateTableA(sessionId, databaseId, "Rows",
                                 16, 80, &tableId));

        JET_COLUMNDEF columnDefinition = {};
        columnDefinition.cbStruct = sizeof(columnDefinition);
        columnDefinition.coltyp = JET_coltypLong;
        columnDefinition.grbit = JET_bitColumnNotNULL;
        JET_COLUMNID valueColumnId = 0;
        CheckJet(JetAddColumnA(sessionId, tableId, "Value",
                               &columnDefinition, nullptr, 0,
                               &valueColumnId));

        CheckJet(JetBeginTransaction(sessionId));
        for (int rowIndex = 0; rowIndex < 5; ++rowIndex)
        {
            const int32_t value = startingValue + rowIndex;
            CheckJet(JetPrepareUpdate(sessionId, tableId, JET_prepInsert));
            CheckJet(JetSetColumn(sessionId, tableId, valueColumnId,
                                  &value, sizeof(value), 0, nullptr));
            CheckJet(JetUpdate(sessionId, tableId, nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(sessionId, 0));
        CheckJet(JetCloseTable(sessionId, tableId));
    };

    buildDatabase("Primary.mdb", 100);
    buildDatabase("Secondary.mdb", 200);

    ChildProcess::SignalReady(directory);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

struct TwoDatabasesRegistrar
{
    TwoDatabasesRegistrar()
    {
        RegisterChildEntry(ChildEntryTwoDatabases, &RunTwoDatabasesChild);
    }
};
[[maybe_unused]] static TwoDatabasesRegistrar _twoDatabasesRegistrar;

// Child for the MinRequiredLog scenario: attaches Primary.mdb, writes
// + commits + detaches; then creates + attaches Secondary.mdb LATER in
// the log stream so Secondary's min-required-log generation is higher
// than Primary's.  Result: the log stream contains records that
// pre-date Secondary's attachment.  A subsequent recovery that ONLY
// has Secondary attached doesn't need those early records — the flag
// JET_bitReplayIgnoreLogRecordsBeforeMinRequiredLog tells the engine
// to skip them.
constexpr const char* ChildEntryStaggeredAttach =
    "Recovery.StaggeredAttachCrashedBeforeTerm";

void RunStaggeredAttachChild(const std::filesystem::path& directory)
{
    JET_INSTANCE instanceHandle = JET_instanceNil;
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }

    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ese-tests-child"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle, &sessionId, nullptr, nullptr));

    auto buildAndCommit = [&](const char* fileName, int32_t startingValue,
                              JET_GRBIT createGrbit)
    {
        const auto path = directory / fileName;
        JET_DBID databaseId = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(sessionId, path.string().c_str(),
                                    nullptr, &databaseId, createGrbit));
        JET_TABLEID tableId = JET_tableidNil;
        CheckJet(JetCreateTableA(sessionId, databaseId, "Rows",
                                 16, 80, &tableId));
        JET_COLUMNDEF columnDefinition = {};
        columnDefinition.cbStruct = sizeof(columnDefinition);
        columnDefinition.coltyp = JET_coltypLong;
        columnDefinition.grbit = JET_bitColumnNotNULL;
        JET_COLUMNID valueColumnId = 0;
        CheckJet(JetAddColumnA(sessionId, tableId, "Value",
                               &columnDefinition, nullptr, 0,
                               &valueColumnId));
        CheckJet(JetBeginTransaction(sessionId));
        for (int rowIndex = 0; rowIndex < 5; ++rowIndex)
        {
            const int32_t value = startingValue + rowIndex;
            CheckJet(JetPrepareUpdate(sessionId, tableId, JET_prepInsert));
            CheckJet(JetSetColumn(sessionId, tableId, valueColumnId,
                                  &value, sizeof(value), 0, nullptr));
            CheckJet(JetUpdate(sessionId, tableId, nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(sessionId, 0));
        CheckJet(JetCloseTable(sessionId, tableId));
        CheckJet(JetCloseDatabase(sessionId, databaseId, 0));
        CheckJet(JetDetachDatabaseA(sessionId, path.string().c_str()));
    };

    // Build Primary first, detach.  Then build Secondary later — its
    // attachment records land at a higher log position than
    // Primary's.  Detaching Primary advances its own min-required-log
    // so it no longer cares about earlier records.
    buildAndCommit("Primary.mdb", 100, JET_bitDbOverwriteExisting);
    buildAndCommit("Secondary.mdb", 200, JET_bitDbOverwriteExisting);

    // Re-attach Primary + write more so the log gets additional
    // records.  Leave Primary attached when SIGKILL hits so recovery
    // has work to do.
    {
        const auto primaryPath = directory / "Primary.mdb";
        CheckJet(JetAttachDatabaseA(sessionId,
                                    primaryPath.string().c_str(), 0));
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(sessionId, primaryPath.string().c_str(),
                                  nullptr, &dbid, 0));
        JET_TABLEID tid = JET_tableidNil;
        CheckJet(JetOpenTableA(sessionId, dbid, "Rows",
                               nullptr, 0, 0, &tid));
        JET_COLUMNDEF colDef = {};
        colDef.cbStruct = sizeof(colDef);
        CheckJet(JetGetTableColumnInfoA(sessionId, tid, "Value",
                                        &colDef, sizeof(colDef),
                                        JET_ColInfo));
        CheckJet(JetBeginTransaction(sessionId));
        for (int32_t value = 200; value < 210; ++value)
        {
            CheckJet(JetPrepareUpdate(sessionId, tid, JET_prepInsert));
            CheckJet(JetSetColumn(sessionId, tid, colDef.columnid,
                                  &value, sizeof(value), 0, nullptr));
            CheckJet(JetUpdate(sessionId, tid, nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(sessionId, 0));
        CheckJet(JetCloseTable(sessionId, tid));
        // Leave the database attached at SIGKILL time.
        (void)dbid;
    }

    ChildProcess::SignalReady(directory);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

struct StaggeredAttachRegistrar
{
    StaggeredAttachRegistrar()
    {
        RegisterChildEntry(ChildEntryStaggeredAttach,
                           &RunStaggeredAttachChild);
    }
};
[[maybe_unused]] static StaggeredAttachRegistrar _staggeredAttachRegistrar;

// Child that uses the smallest log file size (1024 KiB = 1 MiB) and
// writes enough data to roll at least one log generation, then commits
// the final transaction.  Result: at SIGKILL time the directory holds
// edb00000001.log (archive) + edb.log (current at gen >= 2).  This is
// what the InferCheckpointFromRstmapDbs path needs — the engine reads
// the DB headers' lGenMinRequired (gen 1) and opens archive log 1.
constexpr const char* ChildEntryRollingLog =
    "Recovery.RollingLogCrashedBeforeTerm";

void RunRollingLogChild(const std::filesystem::path& directory)
{
    JET_INSTANCE instanceHandle = JET_instanceNil;
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }

    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ese-tests-child"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    // 1024 KiB log files — the smallest the engine accepts.  Forces
    // rapid rotation under modest write volume.
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFileSize, 1024, nullptr));

    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle, &sessionId, nullptr, nullptr));

    auto buildDatabase = [&](const char* fileName, int32_t startingValue)
    {
        const auto path = directory / fileName;
        JET_DBID databaseId = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(sessionId, path.string().c_str(),
                                    nullptr, &databaseId,
                                    JET_bitDbOverwriteExisting));
        JET_TABLEID tableId = JET_tableidNil;
        CheckJet(JetCreateTableA(sessionId, databaseId, "Rows",
                                 16, 80, &tableId));

        JET_COLUMNDEF columnDefinition = {};
        columnDefinition.cbStruct = sizeof(columnDefinition);
        columnDefinition.coltyp = JET_coltypLongBinary;
        JET_COLUMNID payloadColumnId = 0;
        CheckJet(JetAddColumnA(sessionId, tableId, "Payload",
                               &columnDefinition, nullptr, 0,
                               &payloadColumnId));

        // Write large rows (~16 KiB each) in batches so the log
        // rolls at least once before we signal ready.
        constexpr int RowCount = 200;
        constexpr uint32_t PayloadBytes = 16 * 1024;
        std::vector<uint8_t> payload(PayloadBytes, 0xCD);
        for (int batchStart = 0; batchStart < RowCount; batchStart += 10)
        {
            CheckJet(JetBeginTransaction(sessionId));
            for (int rowIndex = batchStart;
                 rowIndex < batchStart + 10 && rowIndex < RowCount;
                 ++rowIndex)
            {
                payload[0] = static_cast<uint8_t>(
                    startingValue + rowIndex);
                CheckJet(JetPrepareUpdate(sessionId, tableId, JET_prepInsert));
                CheckJet(JetSetColumn(sessionId, tableId, payloadColumnId,
                                      payload.data(), PayloadBytes,
                                      0, nullptr));
                CheckJet(JetUpdate(sessionId, tableId, nullptr, 0, nullptr));
            }
            CheckJet(JetCommitTransaction(sessionId, 0));
        }
        CheckJet(JetCloseTable(sessionId, tableId));
    };

    buildDatabase("Primary.mdb", 100);
    buildDatabase("Secondary.mdb", 200);

    ChildProcess::SignalReady(directory);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

struct RollingLogRegistrar
{
    RollingLogRegistrar()
    {
        RegisterChildEntry(ChildEntryRollingLog, &RunRollingLogChild);
    }
};
[[maybe_unused]] static RollingLogRegistrar _rollingLogRegistrar;

// Default log file size in KiB (matches the engine's default 5 MiB).
// Explicit setting on every recovery init prevents state leak when
// a prior scenario in the same process changed the global to a
// smaller value (single-instance JetSetSystemParameter is global).
constexpr uint32_t DefaultLogFileSizeKb = 5120;

// JetInit2 bootstrap matching the child's param set.  Returns the
// init result so the caller can assert success or specific error.
JET_ERR InitInstanceForReplayRecovery(JET_INSTANCE& instanceHandle,
                                       const std::filesystem::path& directory,
                                       JET_GRBIT initGrbit,
                                       uint32_t logFileSizeKb = DefaultLogFileSizeKb)
{
    instanceHandle = JET_instanceNil;
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ese-tests-recover"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFileSize,
                                    logFileSizeKb, nullptr));
    return JetInit2(&instanceHandle, initGrbit);
}

// JetInit3 variant: builds a JET_RSTINFO_A from the supplied
// restore-map entries and calls JetInit3A.  Returns the engine's
// init result.  Used by the restore-map-aware Replay* flag tests.
// When logFileSizeKb is non-zero, sets JET_paramLogFileSize to the
// matching size so the parent recovers under the same log size the
// rolling-log child used.
JET_ERR InitInstanceForReplayRecoveryWithRstMap(
    JET_INSTANCE& instanceHandle,
    const std::filesystem::path& directory,
    std::vector<JET_RSTMAP_A>& restoreMap,
    JET_GRBIT initGrbit,
    uint32_t logFileSizeKb = DefaultLogFileSizeKb)
{
    instanceHandle = JET_instanceNil;
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ese-tests-recover"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFileSize,
                                    logFileSizeKb, nullptr));

    JET_RSTINFO_A rstInfo = {};
    rstInfo.cbStruct = sizeof(rstInfo);
    rstInfo.rgrstmap = restoreMap.empty() ? nullptr : restoreMap.data();
    rstInfo.crstmap = static_cast<int32_t>(restoreMap.size());
    return JetInit3A(&instanceHandle, &rstInfo, initGrbit);
}

// RAII wrapper for a bare JET_INSTANCE handle — ensures JetTerm2
// runs on scope exit so a failed scenario doesn't leak engine state
// into the next test in the binary.
class JetInstanceRaii
{
public:
    explicit JetInstanceRaii(JET_INSTANCE handle = JET_instanceNil)
        : _handle(handle) {}
    ~JetInstanceRaii()
    {
        if (_handle != JET_instanceNil)
        {
            (void)JetTerm2(_handle, JET_bitTermComplete);
            _handle = JET_instanceNil;
        }
    }
    JetInstanceRaii(const JetInstanceRaii&) = delete;
    JetInstanceRaii& operator=(const JetInstanceRaii&) = delete;

    JET_INSTANCE* Address()
    {
        return &_handle;
    }
    JET_INSTANCE Get() const
    {
        return _handle;
    }
private:
    JET_INSTANCE _handle = JET_instanceNil;
};

}  // namespace

EseIntegrationScenario(Recovery, CommittedRowsSurviveSigkill)
{
    TemporaryDirectory directory("Recovery.CommittedRowsSurviveSigkill");

    // Spawn the child, let it commit its rows, then SIGKILL it before
    // it has a chance to JetTerm.
    {
        ChildProcess child(ChildEntryCommittedRows, directory.Path());
        child.WaitUntilReady(std::chrono::seconds(20));
        child.Kill();
        child.WaitForExit();
    }

    // Re-open the engine on the same directory.  JetInit replays the
    // log records the child flushed before we killed it, so the five
    // committed rows must surface.
    EseInstance recoveredInstance(directory);
    EseSession session(recoveredInstance);

    const auto databasePath = directory.Path() / "Survivors.mdb";
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.string().c_str(),
                                0));
    JET_DBID recoveredDbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &recoveredDbid, 0));

    JET_TABLEID recoveredTableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), recoveredDbid, "Survivors",
                           nullptr, 0, 0, &recoveredTableId));

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), recoveredTableId, "Value",
                                    &columnDefinition, sizeof(columnDefinition),
                                    JET_ColInfo));
    const auto columnId = columnDefinition.columnid;

    int observed = 0;
    CheckJet(JetMove(session.Handle(), recoveredTableId, JET_MoveFirst, 0));
    do
    {
        int32_t value = 0;
        uint32_t actualSize = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), recoveredTableId, columnId,
                                   &value, sizeof(value), &actualSize, 0, nullptr));
        Require(value >= 100 && value < 105);
        ++observed;
    }
    while (JetMove(session.Handle(), recoveredTableId, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observed == 5);

    CheckJet(JetCloseTable(session.Handle(), recoveredTableId));
}

EseIntegrationScenario(Recovery, UncommittedRowsDiscardedAfterSigkill)
{
    TemporaryDirectory directory("Recovery.UncommittedRowsDiscardedAfterSigkill");

    {
        ChildProcess child(ChildEntryUncommittedRows, directory.Path());
        child.WaitUntilReady(std::chrono::seconds(20));
        child.Kill();
        child.WaitForExit();
    }

    // Reopen on the same directory.  The child created the table and
    // column inside auto-committed DDL transactions (those survive),
    // but the five rows were never committed and must be absent.
    EseInstance recoveredInstance(directory);
    EseSession session(recoveredInstance);

    const auto databasePath = directory.Path() / "Discardable.mdb";
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.string().c_str(),
                                0));
    JET_DBID recoveredDbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &recoveredDbid, 0));

    JET_TABLEID recoveredTableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), recoveredDbid, "Pending",
                           nullptr, 0, 0, &recoveredTableId));

    RequireJetError(JetMove(session.Handle(), recoveredTableId, JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);

    CheckJet(JetCloseTable(session.Handle(), recoveredTableId));
}

EseIntegrationScenario(Recovery, CleanShutdownAndReopenRetainsAllRows)
{
    // Same-process variant: build a database, drop the instance
    // cleanly, then reopen.  Exercises the JetTerm → JetInit →
    // re-attach path the SIGKILL variant sits on top of.
    TemporaryDirectory directory("Recovery.CleanShutdownAndReopenRetainsAllRows");

    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Recovery.mdb");
        EseTable table(database, "Persistent");

        auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < 7; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    EseInstance instance(directory);
    EseSession session(instance);
    const auto databasePath = directory.Path() / "Recovery.mdb";
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.string().c_str(),
                                0));
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &databaseId, 0));

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), databaseId, "Persistent",
                           nullptr, 0, 0, &tableId));

    int observed = 0;
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    do
    {
        ++observed;
    }
    while (JetMove(session.Handle(), tableId, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observed == 7);

    CheckJet(JetCloseTable(session.Handle(), tableId));
}

EseIntegrationScenario(Recovery, ReplayIgnoreMissingDBProceedsWithoutDeletedDatabase)
{
    TemporaryDirectory directory(
        "Recovery.ReplayIgnoreMissingDBProceedsWithoutDeletedDatabase");

    // Child writes Primary.mdb and Secondary.mdb, both committed
    // and both attached at SIGKILL time.
    {
        ChildProcess child(ChildEntryTwoDatabases, directory.Path());
        child.WaitUntilReady(std::chrono::seconds(20));
        child.Kill();
        child.WaitForExit();
    }

    const auto primaryPath = directory.Path() / "Primary.mdb";
    const auto secondaryPath = directory.Path() / "Secondary.mdb";
    Require(std::filesystem::exists(primaryPath));
    Require(std::filesystem::exists(secondaryPath));

    // Delete Secondary.mdb.  The child's log stream references both
    // databases via attach records.  On recovery, the engine sees
    // Secondary.mdb gone from disk; without JET_bitReplayIgnoreMissingDB
    // the FMP for Secondary.mdb stays in "SkippedAttach" and the
    // checkpoint can't advance past the attachment (log.cxx:2898).
    // With the flag, recovery proceeds and the checkpoint advances.
    // Either way JetInit2 itself succeeds — the difference is the
    // post-recovery checkpoint state.  This scenario pins the flag's
    // observable correctness path: recovery completes, Primary's
    // committed rows survive, and Secondary.mdb is genuinely absent
    // (no resurrection from log replay).
    std::filesystem::remove(secondaryPath);

    JetInstanceRaii recoverInstance;
    CheckJet(InitInstanceForReplayRecovery(
                 *recoverInstance.Address(), directory.Path(),
                 JET_bitReplayIgnoreMissingDB));

    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(recoverInstance.Get(), &sessionId,
                              nullptr, nullptr));

    // Primary.mdb attaches and walks every committed row.
    CheckJet(JetAttachDatabaseA(sessionId,
                                primaryPath.string().c_str(), 0));
    JET_DBID primaryDbId = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(sessionId,
                              primaryPath.string().c_str(),
                              nullptr, &primaryDbId, 0));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionId, primaryDbId, "Rows",
                           nullptr, 0, 0, &tableId));

    int observedRows = 0;
    CheckJet(JetMove(sessionId, tableId, JET_MoveFirst, 0));
    do
    {
        ++observedRows;
    }
    while (JetMove(sessionId, tableId, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observedRows == 5);

    CheckJet(JetCloseTable(sessionId, tableId));
    CheckJet(JetCloseDatabase(sessionId, primaryDbId, 0));
    CheckJet(JetDetachDatabaseA(sessionId, primaryPath.string().c_str()));
    CheckJet(JetEndSession(sessionId, 0));
}

EseIntegrationScenario(Recovery, ReplayIgnoreLostLogsAcceptsRecoveryAfterCurrentLogRemoved)
{
    TemporaryDirectory directory(
        "Recovery.ReplayIgnoreLostLogsAcceptsRecoveryAfterCurrentLogRemoved");

    {
        ChildProcess child(ChildEntryTwoDatabases, directory.Path());
        child.WaitUntilReady(std::chrono::seconds(20));
        child.Kill();
        child.WaitForExit();
    }

    // The current-log filename varies between log-extension policies
    // (.jtx vs .log).  Delete both candidates if present so recovery
    // sees no in-flight transaction tail at all.
    const auto currentLogJtx = directory.Path() / "edb.jtx";
    const auto currentLogLog = directory.Path() / "edb.log";
    bool removedAny = false;
    if (std::filesystem::exists(currentLogJtx))
    {
        std::filesystem::remove(currentLogJtx);
        removedAny = true;
    }
    if (std::filesystem::exists(currentLogLog))
    {
        std::filesystem::remove(currentLogLog);
        removedAny = true;
    }
    Require(removedAny);

    // JET_bitReplayIgnoreLostLogs | JET_bitAllowMissingCurrentLog
    // tells the engine to accept the truncated log stream — JetInit2
    // succeeds.  Note: lost-log recovery deliberately leaves the
    // databases marked dirty (the engine can't guarantee consistency
    // when committed log records may have been lost), so a subsequent
    // JetAttachDatabase against Primary.mdb returns
    // JET_errDatabaseDirtyShutdown.  That's the trade-off the flag
    // documents: "no error at JetInit2" not "fully recovered".  The
    // assertion this scenario pins is the JetInit2 outcome itself —
    // a no-flag init on the same on-disk state errors out instead.
    JetInstanceRaii recoverInstance;
    CheckJet(InitInstanceForReplayRecovery(
                 *recoverInstance.Address(), directory.Path(),
                 JET_bitReplayIgnoreLostLogs
                 | JET_bitAllowMissingCurrentLog));

    // Sanity check the engine reports the expected dirty-shutdown
    // state on attach — proves recovery was real (not a no-op fast
    // path that just succeeded because there was nothing to do).
    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(recoverInstance.Get(), &sessionId,
                              nullptr, nullptr));
    const auto primaryPath = directory.Path() / "Primary.mdb";
    const JET_ERR attachAfterLostLogs =
        JetAttachDatabaseA(sessionId,
                           primaryPath.string().c_str(), 0);
    Require(attachAfterLostLogs == JET_errSuccess
            || attachAfterLostLogs == JET_errDatabaseDirtyShutdown);
    if (attachAfterLostLogs == JET_errSuccess)
    {
        // Some lost-log paths leave the database clean enough to
        // attach; if so, detach to leave the instance in a defined
        // state before JetTerm2 in the RAII destructor.
        CheckJet(JetDetachDatabaseA(sessionId,
                                    primaryPath.string().c_str()));
    }
    CheckJet(JetEndSession(sessionId, 0));
}

EseIntegrationScenario(Recovery, ReplayMissingMapEntryDBDefaultsToOriginalPath)
{
    TemporaryDirectory directory(
        "Recovery.ReplayMissingMapEntryDBDefaultsToOriginalPath");

    // Two-database child writes both DBs.
    {
        ChildProcess child(ChildEntryTwoDatabases, directory.Path());
        child.WaitUntilReady(std::chrono::seconds(20));
        child.Kill();
        child.WaitForExit();
    }

    const auto primaryPath = directory.Path() / "Primary.mdb";
    const auto secondaryPath = directory.Path() / "Secondary.mdb";
    Require(std::filesystem::exists(primaryPath));
    Require(std::filesystem::exists(secondaryPath));

    // Build a restore map that ONLY mentions Primary.mdb (rerouted
    // to itself).  Secondary.mdb is referenced by the log records
    // but absent from the map.
    std::string primarySrc = primaryPath.string();
    std::string primaryDst = primaryPath.string();
    JET_RSTMAP_A primaryEntry = {};
    primaryEntry.szDatabaseName = primarySrc.data();
    primaryEntry.szNewDatabaseName = primaryDst.data();
    std::vector<JET_RSTMAP_A> restoreMap = { primaryEntry };

    // JET_bitReplayMissingMapEntryDB defaults the unmapped Secondary
    // to its original path (since the file IS still on disk under
    // that path).  Recovery succeeds and both databases come up.
    JetInstanceRaii recoverInstance;
    CheckJet(InitInstanceForReplayRecoveryWithRstMap(
                 *recoverInstance.Address(), directory.Path(),
                 restoreMap,
                 JET_bitReplayMissingMapEntryDB));

    // Both Primary and Secondary attach + open + walk cleanly after
    // the flagged init.
    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(recoverInstance.Get(), &sessionId,
                              nullptr, nullptr));

    auto walkDatabase = [&](const std::filesystem::path& path,
                            int32_t expectedRowCount)
    {
        CheckJet(JetAttachDatabaseA(sessionId, path.string().c_str(), 0));
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(sessionId, path.string().c_str(),
                                  nullptr, &dbid, 0));
        JET_TABLEID tid = JET_tableidNil;
        CheckJet(JetOpenTableA(sessionId, dbid, "Rows",
                               nullptr, 0, 0, &tid));
        int32_t observedRows = 0;
        CheckJet(JetMove(sessionId, tid, JET_MoveFirst, 0));
        do
        {
            ++observedRows;
        }
        while (JetMove(sessionId, tid, JET_MoveNext, 0)
               != JET_errNoCurrentRecord);
        Require(observedRows == expectedRowCount);
        CheckJet(JetCloseTable(sessionId, tid));
        CheckJet(JetCloseDatabase(sessionId, dbid, 0));
        CheckJet(JetDetachDatabaseA(sessionId, path.string().c_str()));
    };
    walkDatabase(primaryPath, 5);
    walkDatabase(secondaryPath, 5);

    CheckJet(JetEndSession(sessionId, 0));
}

EseIntegrationScenario(Recovery, ReplayInferCheckpointFromRstmapDbsRecoversWithoutChk)
{
    TemporaryDirectory directory(
        "Recovery.ReplayInferCheckpointFromRstmapDbsRecoversWithoutChk");

    // Rolling-log child: smallest possible log file size + ~3 MiB of
    // writes per database forces multiple log generations to exist.
    // Result: archive logs edb00000001.log etc. are present, which
    // is what InferCheckpointFromRstmapDbs needs to open via
    // ErrLGOpenLogFile(eArchiveLog, ...) in logredo.cxx.
    {
        ChildProcess child(ChildEntryRollingLog, directory.Path());
        child.WaitUntilReady(std::chrono::seconds(60));
        child.Kill();
        child.WaitForExit();
    }

    // Delete the checkpoint.  Without a checkpoint the engine
    // normally has to walk from the oldest log generation; the
    // flag tells it to infer the starting log from RSTMAP headers.
    const auto checkpointPath = directory.Path() / "edb.chk";
    Require(std::filesystem::exists(checkpointPath));
    std::filesystem::remove(checkpointPath);
    Require(!std::filesystem::exists(checkpointPath));

    const auto primaryPath = directory.Path() / "Primary.mdb";
    const auto secondaryPath = directory.Path() / "Secondary.mdb";

    std::string primarySrc = primaryPath.string();
    std::string primaryDst = primaryPath.string();
    std::string secondarySrc = secondaryPath.string();
    std::string secondaryDst = secondaryPath.string();
    JET_RSTMAP_A primaryEntry = {};
    primaryEntry.szDatabaseName = primarySrc.data();
    primaryEntry.szNewDatabaseName = primaryDst.data();
    JET_RSTMAP_A secondaryEntry = {};
    secondaryEntry.szDatabaseName = secondarySrc.data();
    secondaryEntry.szNewDatabaseName = secondaryDst.data();
    std::vector<JET_RSTMAP_A> restoreMap = {
        primaryEntry, secondaryEntry
    };

    JetInstanceRaii recoverInstance;
    CheckJet(InitInstanceForReplayRecoveryWithRstMap(
                 *recoverInstance.Address(), directory.Path(),
                 restoreMap,
                 JET_bitReplayInferCheckpointFromRstmapDbs,
                 /*logFileSizeKb*/ 1024));

    // Both DBs recovered cleanly — the engine read the DB headers'
    // lGenMinRequired / lGenLastConsistent from the RSTMAP'd files
    // (rstmap.cxx:LoadCheckpointGenerationFromRstmap) and opened
    // the archive log at that generation.
    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(recoverInstance.Get(), &sessionId,
                              nullptr, nullptr));

    auto walkDatabase = [&](const std::filesystem::path& path,
                            int32_t expectedRowCount)
    {
        CheckJet(JetAttachDatabaseA(sessionId, path.string().c_str(), 0));
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(sessionId, path.string().c_str(),
                                  nullptr, &dbid, 0));
        JET_TABLEID tid = JET_tableidNil;
        CheckJet(JetOpenTableA(sessionId, dbid, "Rows",
                               nullptr, 0, 0, &tid));
        int32_t observedRows = 0;
        CheckJet(JetMove(sessionId, tid, JET_MoveFirst, 0));
        do
        {
            ++observedRows;
        }
        while (JetMove(sessionId, tid, JET_MoveNext, 0)
               != JET_errNoCurrentRecord);
        Require(observedRows == expectedRowCount);
        CheckJet(JetCloseTable(sessionId, tid));
        CheckJet(JetCloseDatabase(sessionId, dbid, 0));
        CheckJet(JetDetachDatabaseA(sessionId, path.string().c_str()));
    };
    walkDatabase(primaryPath, 200);
    walkDatabase(secondaryPath, 200);

    CheckJet(JetEndSession(sessionId, 0));
}

EseIntegrationScenario(Recovery, ReplayIgnoreLogRecordsBeforeMinRequiredLogSkipsPreAttachRecords)
{
    TemporaryDirectory directory(
        "Recovery.ReplayIgnoreLogRecordsBeforeMinRequiredLogSkipsPreAttachRecords");

    // Staggered-attach child: Primary attached first + detached,
    // Secondary created later, then Primary re-attached + written.
    // The log stream contains records from before Secondary's
    // min-required-log generation; on recovery, those records aren't
    // needed for Secondary, and the flag tells the engine to skip
    // them rather than blocking on missing-prior-log requirements.
    {
        ChildProcess child(ChildEntryStaggeredAttach, directory.Path());
        child.WaitUntilReady(std::chrono::seconds(20));
        child.Kill();
        child.WaitForExit();
    }

    const auto primaryPath = directory.Path() / "Primary.mdb";
    const auto secondaryPath = directory.Path() / "Secondary.mdb";
    Require(std::filesystem::exists(primaryPath));
    Require(std::filesystem::exists(secondaryPath));

    // Recovery with the flag set succeeds against the staggered-
    // attachment log stream.
    JetInstanceRaii recoverInstance;
    CheckJet(InitInstanceForReplayRecovery(
                 *recoverInstance.Address(), directory.Path(),
                 JET_bitReplayIgnoreLogRecordsBeforeMinRequiredLog));

    JET_SESID sessionId = JET_sesidNil;
    CheckJet(JetBeginSessionA(recoverInstance.Get(), &sessionId,
                              nullptr, nullptr));

    // Primary was committed twice (5 initial + 10 post-reattach = 15
    // rows in total) and was the only database attached at SIGKILL.
    CheckJet(JetAttachDatabaseA(sessionId,
                                primaryPath.string().c_str(), 0));
    JET_DBID primaryDbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(sessionId, primaryPath.string().c_str(),
                              nullptr, &primaryDbid, 0));
    JET_TABLEID primaryTid = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionId, primaryDbid, "Rows",
                           nullptr, 0, 0, &primaryTid));
    int32_t primaryRows = 0;
    CheckJet(JetMove(sessionId, primaryTid, JET_MoveFirst, 0));
    do
    {
        ++primaryRows;
    }
    while (JetMove(sessionId, primaryTid, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(primaryRows == 15);
    CheckJet(JetCloseTable(sessionId, primaryTid));
    CheckJet(JetCloseDatabase(sessionId, primaryDbid, 0));
    CheckJet(JetDetachDatabaseA(sessionId, primaryPath.string().c_str()));

    // Secondary was detached before SIGKILL and only carries its
    // initial 5 rows.
    CheckJet(JetAttachDatabaseA(sessionId,
                                secondaryPath.string().c_str(), 0));
    JET_DBID secondaryDbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(sessionId, secondaryPath.string().c_str(),
                              nullptr, &secondaryDbid, 0));
    JET_TABLEID secondaryTid = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionId, secondaryDbid, "Rows",
                           nullptr, 0, 0, &secondaryTid));
    int32_t secondaryRows = 0;
    CheckJet(JetMove(sessionId, secondaryTid, JET_MoveFirst, 0));
    do
    {
        ++secondaryRows;
    }
    while (JetMove(sessionId, secondaryTid, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(secondaryRows == 5);
    CheckJet(JetCloseTable(sessionId, secondaryTid));
    CheckJet(JetCloseDatabase(sessionId, secondaryDbid, 0));
    CheckJet(JetDetachDatabaseA(sessionId, secondaryPath.string().c_str()));

    CheckJet(JetEndSession(sessionId, 0));
}
