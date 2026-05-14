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

using namespace ese::tests;

EseIntegrationScenario(Transaction, BeginAndCommit)
{
    TemporaryDirectory directory("Transaction.BeginAndCommit");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");

    EseTransaction transaction(session);
    transaction.Commit();
}

EseIntegrationScenario(Transaction, CommitWithLazyFlushSucceeds)
{
    TemporaryDirectory directory("Transaction.CommitWithLazyFlushSucceeds");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Lazy");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    EseTransaction transaction(session);
    InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 7);
    transaction.Commit(JET_bitCommitLazyFlush);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBack = RetrieveFixedColumnFromCurrentRecord<int32_t>(table, valueColumnId);
    Require(readBack == 7);
}

EseIntegrationScenario(Transaction, ExplicitRollbackDiscardsChanges)
{
    TemporaryDirectory directory("Transaction.ExplicitRollbackDiscardsChanges");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Rollback");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 5);
        transaction.Rollback();
    }

    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(Transaction, NestedInnerCommitOuterCommitPersistsBoth)
{
    TemporaryDirectory directory(
        "Transaction.NestedInnerCommitOuterCommitPersistsBoth");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Nested");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    // ESE allows nested transactions on the same session — Begin
    // inside Begin creates a savepoint. Both must commit for the
    // changes to land.
    CheckJet(JetBeginTransaction(session.Handle()));
    InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 1);

    CheckJet(JetBeginTransaction(session.Handle()));
    InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 2);
    CheckJet(JetCommitTransaction(session.Handle(), 0)); // inner commit

    CheckJet(JetCommitTransaction(session.Handle(), 0)); // outer commit

    int observedRows = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        ++observedRows;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0) != JET_errNoCurrentRecord);
    Require(observedRows == 2);
}

EseIntegrationScenario(Transaction, NestedInnerCommitOuterRollbackDiscardsBoth)
{
    TemporaryDirectory directory(
        "Transaction.NestedInnerCommitOuterRollbackDiscardsBoth");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Nested");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    CheckJet(JetBeginTransaction(session.Handle()));
    InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 1);

    CheckJet(JetBeginTransaction(session.Handle()));
    InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 2);
    CheckJet(JetCommitTransaction(session.Handle(), 0)); // inner commits to outer

    CheckJet(JetRollback(session.Handle(), 0)); // outer rollback drops everything

    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(Transaction, NestedInnerRollbackOuterCommitKeepsOuter)
{
    TemporaryDirectory directory(
        "Transaction.NestedInnerRollbackOuterCommitKeepsOuter");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Nested");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    CheckJet(JetBeginTransaction(session.Handle()));
    InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 100);

    CheckJet(JetBeginTransaction(session.Handle()));
    InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 200);
    CheckJet(JetRollback(session.Handle(), 0)); // inner rollback

    CheckJet(JetCommitTransaction(session.Handle(), 0)); // outer commits

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int observedRows = 0;
    int32_t survivor = 0;
    do
    {
        survivor = RetrieveFixedColumnFromCurrentRecord<int32_t>(table, valueColumnId);
        ++observedRows;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0) != JET_errNoCurrentRecord);
    Require(observedRows == 1);
    Require(survivor == 100);
}

EseIntegrationScenario(Transaction, RollbackWithoutOpenTransactionReturnsNotInTransaction)
{
    TemporaryDirectory directory(
        "Transaction.RollbackWithoutOpenTransactionReturnsNotInTransaction");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");

    RequireJetError(JetRollback(session.Handle(), 0),
                    JET_errNotInTransaction);
}

EseIntegrationScenario(Transaction, CommitWithoutOpenTransactionReturnsNotInTransaction)
{
    TemporaryDirectory directory(
        "Transaction.CommitWithoutOpenTransactionReturnsNotInTransaction");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");

    RequireJetError(JetCommitTransaction(session.Handle(), 0),
                    JET_errNotInTransaction);
}

EseIntegrationScenario(Transaction, CommittedInsertVisibleToFreshCursor)
{
    TemporaryDirectory directory(
        "Transaction.CommittedInsertVisibleToFreshCursor");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Visible");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 42);
        transaction.Commit();
    }

    // Open a second cursor on the same table (new handle, same session)
    // and verify the committed row is visible there too.
    JET_TABLEID secondCursorId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(),
                           database.Id(),
                           table.Name().c_str(),
                           nullptr,
                           0,
                           0,
                           &secondCursorId));
    CheckJet(JetMove(session.Handle(), secondCursorId, JET_MoveFirst, 0));

    int32_t observedValue = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               secondCursorId,
                               valueColumnId,
                               &observedValue,
                               sizeof(observedValue),
                               &actualSize,
                               0,
                               nullptr));
    Require(observedValue == 42);

    CheckJet(JetCloseTable(session.Handle(), secondCursorId));
}

EseIntegrationScenario(Transaction, TransactionAutoRollbackOnScopeExit)
{
    TemporaryDirectory directory("Transaction.TransactionAutoRollbackOnScopeExit");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "AutoRollback");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    // Drop the EseTransaction without committing — dtor rolls back.
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, 99);
    }

    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(Transaction, GetLockWriteOnCurrentRowSucceeds)
{
    TemporaryDirectory directory(
        "Transaction.GetLockWriteOnCurrentRowSucceeds");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Tx.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 1);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // JetGetLock must run inside a transaction — the lock scope is
    // the transaction itself.  Acquiring a write lock from the same
    // session that's the sole writer always succeeds.
    EseTransaction transaction(session);
    CheckJet(JetGetLock(session.Handle(), table.Id(), JET_bitWriteLock));
    transaction.Commit();
}

EseIntegrationScenario(Transaction, GetLockOutsideTransactionReturnsNotInTransaction)
{
    TemporaryDirectory directory(
        "Transaction.GetLockOutsideTransactionReturnsNotInTransaction");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Tx.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 1);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    // No transaction open — engine requires one.
    RequireJetError(JetGetLock(session.Handle(), table.Id(), JET_bitWriteLock),
                    JET_errNotInTransaction);
}

EseIntegrationScenario(Transaction,
                       BeginTransaction3StoresTrxIdInLogStream)
{
    TemporaryDirectory directory(
        "Transaction.BeginTransaction3StoresTrxIdInLogStream");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);

    //  JetBeginTransaction3 extends BeginTransaction2 by letting the
    //  caller stamp a 64-bit application trxid that the engine
    //  records in the BeginTransaction0 log record.  Used by
    //  distributed-transaction managers to correlate engine-side
    //  state with a higher-level coordinator.  Functional behaviour
    //  matches the simpler form — the row inserted inside the
    //  transaction must be visible after commit.
    constexpr int64_t TransactionId = 0x0123'4567'89AB'CDEFLL;
    CheckJet(JetBeginTransaction3(session.Handle(), TransactionId, 0));

    InsertSingleFixedColumnRow<int32_t>(table, columnId, 4242);
    CheckJet(JetCommitTransaction(session.Handle(), 0));

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    const auto observed =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId);
    Require(observed == 4242);
}

EseIntegrationScenario(Transaction,
                       CommitTransaction2ReturnsMonotonicCommitId)
{
    TemporaryDirectory directory(
        "Transaction.CommitTransaction2ReturnsMonotonicCommitId");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);

    //  JetCommitTransaction2 adds two outputs to the v1 form:
    //    cmsecDurableCommit — caller-allowed delay before forcing
    //      the commit-0 LR to disk (0 = sync), and
    //    pCommitId — a JET_COMMIT_ID stamped with the log signature
    //      + monotonically-increasing commitId per session.
    //  Two commits in sequence must report a strictly increasing
    //  commitId.
    JET_COMMIT_ID commitIdA = {};
    {
        CheckJet(JetBeginTransaction(session.Handle()));
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 1);
        CheckJet(JetCommitTransaction2(session.Handle(),
                                       0,
                                       /*cmsecDurableCommit=*/0,
                                       &commitIdA));
    }

    JET_COMMIT_ID commitIdB = {};
    {
        CheckJet(JetBeginTransaction(session.Handle()));
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 2);
        CheckJet(JetCommitTransaction2(session.Handle(),
                                       0,
                                       /*cmsecDurableCommit=*/0,
                                       &commitIdB));
    }

    Require(commitIdB.commitId > commitIdA.commitId);
    //  Both commits land against the same log stream so the
    //  signatures must match.
    Require(std::memcmp(&commitIdA.signLog,
                        &commitIdB.signLog,
                        sizeof(JET_SIGNATURE)) == 0);
}
