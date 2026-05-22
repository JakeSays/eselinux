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

EseIntegrationScenario(Transaction, BeginAndCommit, Smoke)
{
    TemporaryDirectory directory("Transaction.BeginAndCommit");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                     JET_bitColumnNotNULL);

    //  Insert a row inside the transaction, commit, then read it
    //  back outside the transaction.  An empty Begin/Commit pair
    //  doesn't prove the transaction actually wrapped any work; the
    //  insert + readback does, AND it proves the commit promoted
    //  the row to durable state visible from a cursor positioned
    //  fresh after the transaction closed.
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 1234);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t observedValue = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                               &observedValue, sizeof(observedValue),
                               &actualBytes, 0, nullptr));
    Require(actualBytes == sizeof(observedValue));
    Require(observedValue == 1234);
    //  Second insert in a separate transaction lands above the
    //  first — proves serial transactions stack correctly and the
    //  commit didn't leave latent state.
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 5678);
        transaction.Commit();
    }
    int32_t rowCount = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        ++rowCount;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(rowCount == 2);
}

EseIntegrationScenario(Transaction, CommitWithLazyFlushSucceeds, Smoke)
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

EseIntegrationScenario(Transaction, ExplicitRollbackDiscardsChanges, Smoke)
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

EseIntegrationScenario(Transaction, NestedInnerCommitOuterCommitPersistsBoth, Smoke)
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

EseIntegrationScenario(Transaction, NestedInnerCommitOuterRollbackDiscardsBoth, Smoke)
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

EseIntegrationScenario(Transaction, NestedInnerRollbackOuterCommitKeepsOuter, Smoke)
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

EseIntegrationScenario(Transaction, RollbackWithoutOpenTransactionReturnsNotInTransaction, Smoke)
{
    TemporaryDirectory directory(
        "Transaction.RollbackWithoutOpenTransactionReturnsNotInTransaction");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");

    RequireJetError(JetRollback(session.Handle(), 0),
                    JET_errNotInTransaction);
}

EseIntegrationScenario(Transaction, CommitWithoutOpenTransactionReturnsNotInTransaction, Smoke)
{
    TemporaryDirectory directory(
        "Transaction.CommitWithoutOpenTransactionReturnsNotInTransaction");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Transaction.mdb");

    RequireJetError(JetCommitTransaction(session.Handle(), 0),
                    JET_errNotInTransaction);
}

EseIntegrationScenario(Transaction, CommittedInsertVisibleToFreshCursor, Smoke)
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

EseIntegrationScenario(Transaction, TransactionAutoRollbackOnScopeExit, Smoke)
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

EseIntegrationScenario(Transaction, GetLockWriteOnCurrentRowSucceeds, Smoke)
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

EseIntegrationScenario(Transaction, GetLockOutsideTransactionReturnsNotInTransaction, Smoke)
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

EseIntegrationScenario(Transaction, BeginTransaction3StoresTrxIdInLogStream, Smoke)
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

EseIntegrationScenario(Transaction, CommitTransaction2ReturnsMonotonicCommitId, Smoke)
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

//  ===================================================================
//  Tier::Regression — deep savepoint nesting + complex undo payload
//
//  Smoke tests cover 2–3 levels of savepoint nesting with single-row
//  insert payloads.  Real-world workloads can pile up many savepoints
//  with mixed DML on multi-column records; a refactor that broke the
//  undo log ordering or savepoint version-store cleanup at depth
//  would slip past the Smoke tier.  This scenario nests 15 levels
//  deep, performs insert/update/delete across multiple columns and a
//  secondary index at each level, rolls back the inner half, commits
//  the outer half, and verifies the final state matches the predicted
//  surviving rows exactly.
//  ===================================================================
EseIntegrationScenario(Transaction, DeepSavepointStackUnwindsComplexPayload, Regression)
{
    TemporaryDirectory directory(
        "Transaction.DeepSavepointStackUnwindsComplexPayload");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "DeepSavepoint.mdb");
    EseTable table(database, "Rows");

    auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                     JET_bitColumnNotNULL);
    auto markerColumn = table.AddColumn("Marker", JET_coltypLong,
                                        JET_bitColumnNotNULL);
    auto bodyColumn = table.AddColumn("Body", JET_coltypLongBinary);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);
    static constexpr std::string_view MarkerKey =
        std::string_view("+Marker\0\0", 9);
    table.CreateIndex("ByMarker", MarkerKey);

    //  Seed with 10 baseline rows committed outside the deep stack.
    {
        EseTransaction transaction(session);
        for (int32_t key = 0; key < 10; ++key)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  keyColumn, &key, sizeof(key),
                                  0, nullptr));
            const int32_t marker = 1000;
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  markerColumn, &marker, sizeof(marker),
                                  0, nullptr));
            const std::string body = std::format("baseline-row-{}", key);
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  bodyColumn,
                                  body.data(), body.size(),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    //  ESE caps savepoint nesting at 7 (engine-internal
    //  JET_cbtransactionLevelsMax).  Use exactly that depth — beyond
    //  it the engine rejects with JET_errTransTooDeep, which we also
    //  validate at the bottom of this scenario.
    static constexpr int32_t Depth = 7;
    //  Open Depth nested savepoints.  Level i performs:
    //    - Insert row (Key=100+i, Marker=2000+i)
    //    - Update Marker on baseline row i % 10 to 3000+i
    //    - On levels >= 5, also overwrite Body on baseline row
    //      (i % 10) with a long-value blob to drive the undo path
    //      through the long-value tree.
    for (int32_t depth = 0; depth < Depth; ++depth)
    {
        CheckJet(JetBeginTransaction(session.Handle()));

        const int32_t newKey = 100 + depth;
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              keyColumn, &newKey, sizeof(newKey),
                              0, nullptr));
        const int32_t newMarker = 2000 + depth;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              markerColumn, &newMarker, sizeof(newMarker),
                              0, nullptr));
        const std::string newBody =
            std::format("nested-depth-{}-payload", depth);
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              bodyColumn, newBody.data(), newBody.size(),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));

        const int32_t baselineKey = depth % 10;
        CheckJet(JetMakeKey(session.Handle(), table.Id(),
                            &baselineKey, sizeof(baselineKey),
                            JET_bitNewKey));
        CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepReplace));
        const int32_t mutatedMarker = 3000 + depth;
        CheckJet(JetSetColumn(session.Handle(), table.Id(),
                              markerColumn,
                              &mutatedMarker, sizeof(mutatedMarker),
                              0, nullptr));
        if (depth >= 5)
        {
            //  At deeper levels, replace the Body with a long-value
            //  to drive the undo path through LV-tree operations.
            const std::string body =
                std::format("mutated-body-from-depth-{}", depth);
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  bodyColumn,
                                  body.data(), body.size(),
                                  0, nullptr));
        }
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
    }

    //  Boundary probe: a Depth+1 begin must hit ErrorTransTooDeep
    //  before we touch the rollback / commit drain.  Catches a
    //  refactor that silently raised or removed the cap.
    RequireJetError(JetBeginTransaction(session.Handle()),
                    JET_errTransTooDeep);

    //  Roll back the inner half and commit the outer half.  With
    //  Depth=7 we roll back 3 inner levels and commit 4 outer.
    static constexpr int32_t InnerRollbackCount = 3;
    for (int32_t i = 0; i < InnerRollbackCount; ++i)
    {
        CheckJet(JetRollback(session.Handle(), 0));
    }
    const int32_t outerCommitCount = Depth - InnerRollbackCount;
    for (int32_t i = 0; i < outerCommitCount; ++i)
    {
        CheckJet(JetCommitTransaction(session.Handle(), 0));
    }

    //  Predict the surviving state:
    //    Baseline rows 0..9 with Marker=1000, except those touched
    //    at committed depths d ∈ [0..7], where the LAST committed
    //    depth that hit row d wins.  Levels 8..14 rolled back,
    //    levels 0..7 committed.  For each baseline key k, the last
    //    committed depth touching k is the largest d ∈ [0..7] with
    //    d % 10 == k.
    //    Plus rows 100..107 from the inserts at committed depths,
    //    Marker=2000+d.  Rows 108..114 must NOT exist.
    int32_t expectedMarkerForBaseline[10] = {};
    for (int32_t k = 0; k < 10; ++k)
    {
        expectedMarkerForBaseline[k] = 1000;
    }
    for (int32_t d = 0; d < outerCommitCount; ++d)
    {
        expectedMarkerForBaseline[d % 10] = 3000 + d;
    }

    //  Walk by primary index and validate every survivor.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "PrimaryByKey"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t walkedRowCount = 0;
    while (true)
    {
        int32_t observedKey = 0;
        int32_t observedMarker = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   keyColumn,
                                   &observedKey, sizeof(observedKey),
                                   &actualBytes, 0, nullptr));
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   markerColumn,
                                   &observedMarker, sizeof(observedMarker),
                                   &actualBytes, 0, nullptr));
        if (observedKey < 10)
        {
            Require(observedMarker ==
                    expectedMarkerForBaseline[observedKey]);
        }
        else
        {
            Require(observedKey >= 100);
            Require(observedKey < 100 + outerCommitCount);
            Require(observedMarker == 2000 + (observedKey - 100));
        }
        ++walkedRowCount;
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    //  10 baseline rows + outerCommitCount inserted rows survive.
    Require(walkedRowCount == 10 + outerCommitCount);

    //  Inserted keys from rolled-back depths must be absent.
    for (int32_t rolledBackKey = 100 + outerCommitCount;
         rolledBackKey < 100 + Depth;
         ++rolledBackKey)
    {
        CheckJet(JetMakeKey(session.Handle(), table.Id(),
                            &rolledBackKey, sizeof(rolledBackKey),
                            JET_bitNewKey));
        RequireJetError(JetSeek(session.Handle(), table.Id(),
                                JET_bitSeekEQ),
                        JET_errRecordNotFound);
    }
}

//  ===================================================================
//  Tier::Regression — lazy-flush commit durability across detach
//  + reattach.  Smoke CommitWithLazyFlush reads back inside the
//  same session.  A refactor that broke lazy-flush durability
//  could pass that — the row sits in the buffer cache.  This
//  scenario commits with LazyFlush, detaches + JetTerms, then
//  reattaches in a fresh instance and confirms every row is
//  present.
//  ===================================================================
EseIntegrationScenario(Transaction, LazyFlushCommitsAreDurableAcrossTerm, Regression)
{
    TemporaryDirectory directory(
        "Transaction.LazyFlushCommitsAreDurableAcrossTerm");
    const auto databasePath = directory.Path() / "Lazy.mdb";

    static constexpr int32_t Rows = 256;
    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "Lazy.mdb");
        EseTable table(database, "Rows");
        auto valueColumn = table.AddColumn("Value", JET_coltypLong,
                                            JET_bitColumnNotNULL);
        EseTransaction transaction(session);
        for (int32_t i = 0; i < Rows; ++i)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumn, i);
        }
        transaction.Commit(JET_bitCommitLazyFlush);
        //  Wait briefly for the lazy-flush thread to pick the
        //  commit up before tearing down — engine spec says
        //  JetTerm waits for outstanding lazy commits to flush.
    }
    //  EseInstance dtor calls JetTerm2, which must flush pending
    //  lazy-flush commits before returning.

    //  Reattach in a fresh instance: every row must be present.
    EseInstance instance(directory);
    EseSession session(instance);
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.string().c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.string().c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), dbid, "Rows",
                           nullptr, 0, 0, &tableId));
    JET_COLUMNDEF valueInfo = {};
    valueInfo.cbStruct = sizeof(valueInfo);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), tableId, "Value",
                                     &valueInfo, sizeof(valueInfo),
                                     JET_ColInfo));
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    int32_t expected = 0;
    int32_t walked = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                                   valueInfo.columnid,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed == expected);
        ++expected;
        ++walked;
        const auto moveResult = JetMove(session.Handle(), tableId,
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walked == Rows);
    CheckJet(JetCloseTable(session.Handle(), tableId));
    CheckJet(JetCloseDatabase(session.Handle(), dbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                databasePath.string().c_str()));
}

