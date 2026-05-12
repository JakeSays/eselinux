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
