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
#include <vector>

using namespace ese::tests;

EseIntegrationScenario(Session, OpenAndClose, Smoke)
{
    TemporaryDirectory directory("Session.OpenAndClose");
    EseInstance instance(directory);
    EseSession session(instance);
    Require(session.Handle() != JET_sesidNil);

    //  A live session must accept JetBeginTransaction / Rollback —
    //  exercise the contract that the handle is in a usable state,
    //  not just that JetBeginSessionA returned success.
    CheckJet(JetBeginTransaction(session.Handle()));
    //  Inside a transaction, GetSessionInfo reports a non-zero
    //  trx-level (one nesting level deep).
    JET_SESSIONINFO insideTrx = {};
    CheckJet(JetGetSessionInfo(session.Handle(),
                               &insideTrx, sizeof(insideTrx),
                               JET_SessionInfo));
    Require(insideTrx.ulTrxLevel == 1);
    CheckJet(JetRollback(session.Handle(), 0));

    //  After rollback the transaction level must drop back to zero.
    //  This proves both that GetSessionInfo reads live state and
    //  that Rollback unwound the nesting we just opened.
    JET_SESSIONINFO afterRollback = {};
    CheckJet(JetGetSessionInfo(session.Handle(),
                               &afterRollback, sizeof(afterRollback),
                               JET_SessionInfo));
    Require(afterRollback.ulTrxLevel == 0);
}

EseIntegrationScenario(Session, DupSessionYieldsDistinctSesid, Smoke)
{
    TemporaryDirectory directory("Session.DupSessionYieldsDistinctSesid");
    EseInstance instance(directory);
    EseSession session(instance);

    // JetDupSession returns a sibling sesid that addresses the same
    // underlying user session: distinct value, both must work, and
    // closing one must NOT invalidate the other.
    JET_SESID duplicateSesid = JET_sesidNil;
    CheckJet(JetDupSession(session.Handle(), &duplicateSesid));
    Require(duplicateSesid != JET_sesidNil);
    Require(duplicateSesid != session.Handle());

    CheckJet(JetEndSession(duplicateSesid, 0));
}

EseIntegrationScenario(Session, DupCursorYieldsIndependentCursor, Smoke)
{
    TemporaryDirectory directory(
        "Session.DupCursorYieldsIndependentCursor");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Session.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < 10; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, rowIndex);
        }
        transaction.Commit();
    }

    JET_TABLEID secondCursor = JET_tableidNil;
    CheckJet(JetDupCursor(session.Handle(), table.Id(),
                          &secondCursor, 0));
    Require(secondCursor != JET_tableidNil);
    Require(secondCursor != table.Id());

    // Move both cursors to their respective extremes; their positions
    // must not affect each other.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), secondCursor, JET_MoveLast, 0));

    const auto firstValue = RetrieveFixedColumnFromCurrentRecord<int32_t>(
                                table, columnId);
    int32_t lastValue = 0;
    uint32_t cbActual = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), secondCursor, columnId,
                               &lastValue, sizeof(lastValue),
                               &cbActual, 0, nullptr));

    Require(firstValue == 0);
    Require(lastValue == 9);

    CheckJet(JetCloseTable(session.Handle(), secondCursor));
}

EseIntegrationScenario(Session, GetSessionInfoReportsTxLevel, Smoke)
{
    TemporaryDirectory directory("Session.GetSessionInfoReportsTxLevel");
    EseInstance instance(directory);
    EseSession session(instance);

    JET_SESSIONINFO baseline = {};
    CheckJet(JetGetSessionInfo(session.Handle(), &baseline,
                               sizeof(baseline), JET_SessionInfo));
    // Outside any transaction, the level is 0.
    Require(baseline.ulTrxLevel == 0);

    {
        EseTransaction outer(session);
        JET_SESSIONINFO insideOuter = {};
        CheckJet(JetGetSessionInfo(session.Handle(), &insideOuter,
                                   sizeof(insideOuter), JET_SessionInfo));
        Require(insideOuter.ulTrxLevel == 1);

        {
            EseTransaction inner(session);
            JET_SESSIONINFO insideInner = {};
            CheckJet(JetGetSessionInfo(session.Handle(), &insideInner,
                                       sizeof(insideInner), JET_SessionInfo));
            Require(insideInner.ulTrxLevel == 2);
            inner.Rollback();
        }

        // Inner closed, outer still open — back to level 1.
        JET_SESSIONINFO afterInner = {};
        CheckJet(JetGetSessionInfo(session.Handle(), &afterInner,
                                   sizeof(afterInner), JET_SessionInfo));
        Require(afterInner.ulTrxLevel == 1);

        outer.Rollback();
    }

    JET_SESSIONINFO afterOuter = {};
    CheckJet(JetGetSessionInfo(session.Handle(), &afterOuter,
                               sizeof(afterOuter), JET_SessionInfo));
    Require(afterOuter.ulTrxLevel == 0);
}

EseIntegrationScenario(Session, GetThreadStatsAccumulatesOnInsert, Smoke)
{
    TemporaryDirectory directory("Session.GetThreadStatsAccumulatesOnInsert");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Session.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);

    JET_THREADSTATS baseline = {};
    baseline.cbStruct = sizeof(baseline);
    CheckJet(JetGetThreadStats(&baseline, sizeof(baseline)));

    {
        EseTransaction transaction(session);
        for (int i = 0; i < 50; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, i);
        }
        transaction.Commit();
    }

    JET_THREADSTATS after = {};
    after.cbStruct = sizeof(after);
    CheckJet(JetGetThreadStats(&after, sizeof(after)));

    // The inserts produce log records and dirty pages; the counters
    // are cumulative per-thread, so they must have advanced.
    Require(after.cLogRecord > baseline.cLogRecord);
    Require(after.cbLogRecord > baseline.cbLogRecord);
    Require(after.cPageDirtied >= baseline.cPageDirtied);
}

EseIntegrationScenario(Session, GetVersionReturnsNonZero, Smoke)
{
    TemporaryDirectory directory("Session.GetVersionReturnsNonZero");
    EseInstance instance(directory);
    EseSession session(instance);

    uint32_t version = 0;
    CheckJet(JetGetVersion(session.Handle(), &version));
    // The engine encodes the build number in the version word; the
    // value must be non-zero (rules out "API never populated the
    // output").  Specific bit layout varies across major versions,
    // so we don't pin which half encodes what.
    Require(version != 0);

    //  Two successive calls on the same engine must report the same
    //  version (it's a property of the binary, not per-call state).
    uint32_t versionAgain = 0;
    CheckJet(JetGetVersion(session.Handle(), &versionAgain));
    Require(versionAgain == version);
}

EseIntegrationScenario(Session, GetCursorInfoChecksCurrentRecordLock, Smoke)
{
    TemporaryDirectory directory("Session.GetCursorInfoChecksCurrentRecordLock");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Session.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 42);
        transaction.Commit();
    }

    // The engine's JetGetCursorInfo contract is undocumented externally:
    // pvResult/cbMax must both be NULL/0, InfoLevel must be 0, and the
    // cursor must be positioned on a current record.  The call then
    // succeeds when no other session is updating that record (the
    // engine uses it as a write-conflict probe internally).
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetGetCursorInfo(session.Handle(), table.Id(),
                              nullptr, 0, 0));

    // Without a current record (after moving past end) the call must
    // surface JET_errNoCurrentRecord.
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord)
    {
    }
    RequireJetError(JetGetCursorInfo(session.Handle(), table.Id(),
                                     nullptr, 0, 0),
                    JET_errNoCurrentRecord);

    // Non-zero cbMax / InfoLevel must be rejected with InvalidParameter.
    uint8_t scratch[4] = {};
    RequireJetError(JetGetCursorInfo(session.Handle(), table.Id(),
                                     scratch, sizeof(scratch), 0),
                    JET_errInvalidParameter);
}

EseIntegrationScenario(Session, ClosingOneDupCursorLeavesOthersUsable, Smoke)
{
    TemporaryDirectory directory(
        "Session.ClosingOneDupCursorLeavesOthersUsable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Session.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 42);
        transaction.Commit();
    }

    // Two dups in addition to the original.  Close the first dup;
    // the second dup and the original must still operate.
    JET_TABLEID dupA = JET_tableidNil;
    JET_TABLEID dupB = JET_tableidNil;
    CheckJet(JetDupCursor(session.Handle(), table.Id(), &dupA, 0));
    CheckJet(JetDupCursor(session.Handle(), table.Id(), &dupB, 0));
    Require(dupA != dupB);
    Require(dupA != table.Id());
    Require(dupB != table.Id());

    CheckJet(JetCloseTable(session.Handle(), dupA));

    // The other two cursors keep working.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), dupB, JET_MoveFirst, 0));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId) == 42);

    int32_t value = 0;
    uint32_t cbActual = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), dupB, columnId,
                               &value, sizeof(value), &cbActual, 0, nullptr));
    Require(value == 42);

    CheckJet(JetCloseTable(session.Handle(), dupB));
}

EseIntegrationScenario(Session, SetAndResetSessionContextRoundTrip, Smoke)
{
    TemporaryDirectory directory(
        "Session.SetAndResetSessionContextRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);

    // SetSessionContext pins a JET_API_PTR-sized cookie to the
    // current thread so the engine can route subsequent calls
    // through the same session reliably.  The pair forms a
    // "borrow / return" handoff; double-set without reset surfaces
    // as JET_errSessionContextAlreadySet.
    CheckJet(JetSetSessionContext(session.Handle(),
                                  static_cast<JET_API_PTR>(0xC0FFEE)));
    RequireJetError(JetSetSessionContext(session.Handle(),
                                         static_cast<JET_API_PTR>(0xBADC0DE)),
                    JET_errSessionContextAlreadySet);
    CheckJet(JetResetSessionContext(session.Handle()));

    // A fresh set after reset must succeed; same for the reset.
    CheckJet(JetSetSessionContext(session.Handle(),
                                  static_cast<JET_API_PTR>(0xFEEDFACE)));
    CheckJet(JetResetSessionContext(session.Handle()));
}

namespace
{

// Free-LS callback for the Set/Get LS scenario.  The engine invokes
// this once per cursor/table when the LS would otherwise leak.
JET_ERR JET_API FreeLSCallback(JET_SESID    /*sesid*/,
                               JET_DBID     /*dbid*/,
                               JET_TABLEID  /*tableid*/,
                               JET_CBTYP    /*cbtyp*/,
                               void*        /*pvArg1*/,
                               void*        /*pvArg2*/,
                               void*        /*pvContext*/,
                               JET_API_PTR  /*ulUnused*/)
{
    // We use integer sentinel values for LS — nothing to free.
    return JET_errSuccess;
}

} // namespace

EseIntegrationScenario(Session, SetAndGetCursorLocalStorageRoundTrip, Smoke)
{
    TemporaryDirectory directory(
        "Session.SetAndGetCursorLocalStorageRoundTrip");
    // JetSetLS / JetGetLS require JET_paramRuntimeCallback set on the
    // instance before JetInit — without it the engine returns
    // JET_errLSCallbackNotSpecified.
    EseInstance instance(directory, "ese-tests", FreeLSCallback);
    EseSession session(instance);
    EseDatabase database(session, "Session.mdb");
    EseTable table(database, "Rows");

    // Engine reports "no LS attached" via JET_errLSNotSet, not by
    // writing JET_LSNil into pls.
    JET_LS lsBaseline = 0;
    RequireJetError(JetGetLS(session.Handle(), table.Id(),
                             &lsBaseline, JET_bitLSCursor),
                    JET_errLSNotSet);

    // Attach a sentinel value to the cursor's local storage.
    constexpr JET_LS Sentinel = static_cast<JET_LS>(0xDEADBEEF);
    CheckJet(JetSetLS(session.Handle(), table.Id(),
                      Sentinel, JET_bitLSCursor));

    JET_LS lsObserved = 0;
    CheckJet(JetGetLS(session.Handle(), table.Id(),
                      &lsObserved, JET_bitLSCursor));
    Require(lsObserved == Sentinel);

    // JET_bitLSReset on Get returns the prior value and clears it.
    JET_LS lsResetRead = 0;
    CheckJet(JetGetLS(session.Handle(), table.Id(),
                      &lsResetRead, JET_bitLSCursor | JET_bitLSReset));
    Require(lsResetRead == Sentinel);

    // Subsequent Get without Reset surfaces LSNotSet again.
    JET_LS lsAfterReset = 0;
    RequireJetError(JetGetLS(session.Handle(), table.Id(),
                             &lsAfterReset, JET_bitLSCursor),
                    JET_errLSNotSet);
}

EseIntegrationScenario(Session, SetAndGetSessionParameterRoundTrip, Smoke)
{
    TemporaryDirectory directory(
        "Session.SetAndGetSessionParameterRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);

    //  JET_sesparamCorrelationID is a uint32 tag the client gets to
    //  set per-session — the engine echoes it in traces and stores
    //  nothing else.  Set, read back, confirm.
    constexpr uint32_t CorrelationId = 0xDEADBEEF;
    CheckJet(JetSetSessionParameter(session.Handle(),
                                    JET_sesparamCorrelationID,
                                    const_cast<uint32_t*>(&CorrelationId),
                                    sizeof(CorrelationId)));

    uint32_t observed = 0;
    uint32_t cbActual = 0;
    CheckJet(JetGetSessionParameter(session.Handle(),
                                    JET_sesparamCorrelationID,
                                    &observed,
                                    sizeof(observed),
                                    &cbActual));
    Require(cbActual == sizeof(observed));
    Require(observed == CorrelationId);
}

EseIntegrationScenario(Session, GetSystemParameterReportsConfiguredBaseName, Smoke)
{
    TemporaryDirectory directory(
        "Session.GetSystemParameterReportsConfiguredBaseName");
    EseInstance instance(directory);

    //  JET_paramBaseName is a 3-char string EseInstance configures at
    //  init.  JetGetSystemParameter mirrors JetSetSystemParameter's
    //  shape — caller passes a JET_API_PTR* slot AND a string buffer;
    //  string params populate the buffer, numeric params populate
    //  *plParam.  Used here to confirm the framework's baseline.
    JET_API_PTR lParam = 0;
    char baseName[16] = {};
    CheckJet(JetGetSystemParameterA(instance.Handle(),
                                    JET_sesidNil,
                                    JET_paramBaseName,
                                    &lParam,
                                    baseName,
                                    sizeof(baseName)));
    Require(std::strlen(baseName) >= 1);
}

//  ===================================================================
//  Tier::Regression — DupCursor produces independent cursor state.
//
//  Smoke DupCursor moves both cursors to the same place and reads
//  once.  A refactor that shared internal position cache across
//  dup'd cursors would still pass that.  This scenario positions
//  cursor A on row N, dups to cursor B, moves B independently to
//  row M, and asserts A's position is unaffected.
//  ===================================================================
EseIntegrationScenario(Session, DupCursorPositionIsolatedFromOriginal, Regression)
{
    TemporaryDirectory directory(
        "Session.DupCursorPositionIsolatedFromOriginal");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "DupPos.mdb");
    EseTable table(database, "Rows");
    auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                       JET_bitColumnNotNULL);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 100; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumn, i);
        }
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
    //  Cursor on row index 2 (Value=2).
    JET_TABLEID dup = JET_tableidNil;
    CheckJet(JetDupCursor(session.Handle(), table.Id(), &dup, 0));

    //  Move dup independently to last row.
    CheckJet(JetMove(session.Handle(), dup, JET_MoveLast, 0));
    int32_t observedDup = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), dup, valueColumn,
                               &observedDup, sizeof(observedDup),
                               &actualBytes, 0, nullptr));
    Require(observedDup == 99);

    //  Original cursor must still be on Value=2.
    int32_t observedOriginal = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), valueColumn,
                               &observedOriginal, sizeof(observedOriginal),
                               &actualBytes, 0, nullptr));
    Require(observedOriginal == 2);

    //  Close the duplicate; the original must still be usable.
    CheckJet(JetCloseTable(session.Handle(), dup));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), valueColumn,
                               &observedOriginal, sizeof(observedOriginal),
                               &actualBytes, 0, nullptr));
    Require(observedOriginal == 3);
}

//  ===================================================================
//  Tier::Regression — write-conflict surfaces across sessions.
//
//  Smoke GetCursorInfo probes a single-session lock.  A refactor
//  that broke multi-session write isolation would pass that.
//  Two sessions both update the same row inside open transactions;
//  the second update must surface JET_errWriteConflict.
//  ===================================================================
EseIntegrationScenario(Session, MultiSessionWriteConflictRejectsConcurrent, Regression)
{
    TemporaryDirectory directory(
        "Session.MultiSessionWriteConflictRejectsConcurrent");
    EseInstance instance(directory);
    {
        EseSession setup(instance);
        EseDatabase database(setup, "Conflict.mdb");
        EseTable table(database, "Rows");
        auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                           JET_bitColumnNotNULL);
        EseTransaction transaction(setup);
        InsertSingleFixedColumnRow<int32_t>(table, valueColumn, 1);
        transaction.Commit();
    }

    EseSession sessionA(instance);
    EseDatabase dbA(sessionA, "Conflict.mdb",
                    EseDatabaseMode::AttachAndOpen);
    EseTable tableA(dbA, "Rows", EseTableMode::Open);
    JET_COLUMNDEF valueInfo = {};
    valueInfo.cbStruct = sizeof(valueInfo);
    CheckJet(JetGetTableColumnInfoA(sessionA.Handle(), tableA.Id(),
                                     "Value", &valueInfo, sizeof(valueInfo),
                                     JET_ColInfo));

    EseSession sessionB(instance);
    EseDatabase dbB(sessionB, "Conflict.mdb",
                    EseDatabaseMode::AttachAndOpen);
    EseTable tableB(dbB, "Rows", EseTableMode::Open);

    //  Session A begins txn + replaces.
    CheckJet(JetBeginTransaction(sessionA.Handle()));
    CheckJet(JetMove(sessionA.Handle(), tableA.Id(), JET_MoveFirst, 0));
    CheckJet(JetPrepareUpdate(sessionA.Handle(), tableA.Id(),
                              JET_prepReplace));
    const int32_t newA = 100;
    CheckJet(JetSetColumn(sessionA.Handle(), tableA.Id(),
                          valueInfo.columnid,
                          &newA, sizeof(newA), 0, nullptr));
    CheckJet(JetUpdate(sessionA.Handle(), tableA.Id(),
                       nullptr, 0, nullptr));

    //  Session B begins txn + tries to replace the same row.  The
    //  engine surfaces JET_errWriteConflict at the
    //  JetPrepareUpdate(JET_prepReplace) call — that's where it
    //  acquires the row's version-store lock.
    CheckJet(JetBeginTransaction(sessionB.Handle()));
    CheckJet(JetMove(sessionB.Handle(), tableB.Id(), JET_MoveFirst, 0));
    RequireJetError(JetPrepareUpdate(sessionB.Handle(), tableB.Id(),
                                     JET_prepReplace),
                    JET_errWriteConflict);
    CheckJet(JetRollback(sessionB.Handle(), 0));

    //  Session A commits; new value sticks.
    CheckJet(JetCommitTransaction(sessionA.Handle(), 0));
    CheckJet(JetMove(sessionA.Handle(), tableA.Id(), JET_MoveFirst, 0));
    int32_t observed = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(sessionA.Handle(), tableA.Id(),
                               valueInfo.columnid,
                               &observed, sizeof(observed),
                               &actualBytes, 0, nullptr));
    Require(observed == newA);
}

//  ===================================================================
//  Tier::Regression — many JetDupCursor handles open simultaneously.
//  Smoke ClosingOneDupCursor closes the first dup and uses the
//  others.  This scenario opens 64 dup cursors, navigates each to
//  a distinct row, reads each back, then closes them all — catches
//  refactors that broke dup-list cleanup or hit resource caps.
//  ===================================================================
EseIntegrationScenario(Session, ManyDupCursorsCoexistAndIsolatePositions, Regression)
{
    TemporaryDirectory directory(
        "Session.ManyDupCursorsCoexistAndIsolatePositions");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Dups.mdb");
    EseTable table(database, "Rows");
    auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                       JET_bitColumnNotNULL);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 128; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumn, i);
        }
        transaction.Commit();
    }

    static constexpr int32_t DupCount = 64;
    std::vector<JET_TABLEID> dups(DupCount, JET_tableidNil);
    for (int32_t i = 0; i < DupCount; ++i)
    {
        CheckJet(JetDupCursor(session.Handle(), table.Id(),
                              &dups[static_cast<size_t>(i)], 0));
        Require(dups[static_cast<size_t>(i)] != JET_tableidNil);
        //  Each dup walks to row index i.
        CheckJet(JetMove(session.Handle(),
                         dups[static_cast<size_t>(i)],
                         JET_MoveFirst, 0));
        for (int32_t step = 0; step < i; ++step)
        {
            CheckJet(JetMove(session.Handle(),
                             dups[static_cast<size_t>(i)],
                             JET_MoveNext, 0));
        }
    }
    //  Read each dup's current row — must observe its assigned
    //  Value, proving cursor-state isolation across 64 dups.
    for (int32_t i = 0; i < DupCount; ++i)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(),
                                   dups[static_cast<size_t>(i)],
                                   valueColumn,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed == i);
    }
    //  Close in REVERSE order so the dup-list pointer maintenance
    //  is exercised at every depth.
    for (int32_t i = DupCount - 1; i >= 0; --i)
    {
        CheckJet(JetCloseTable(session.Handle(),
                               dups[static_cast<size_t>(i)]));
    }
    //  Original cursor must still be usable.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t observed = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), valueColumn,
                               &observed, sizeof(observed),
                               &actualBytes, 0, nullptr));
    Require(observed == 0);
}
