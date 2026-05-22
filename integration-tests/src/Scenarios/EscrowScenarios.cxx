// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

namespace
{

// Insert one row into the table whose only payload is the escrow
// column already added by the caller.  The escrow column's default
// value supplies the initial counter.
void InsertSingleEscrowRow(EseSession& session, EseTable& table)
{
    EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();
}

// Read the escrow column's current value off the current record.
int32_t RetrieveLong(EseSession& session, EseTable& table, JET_COLUMNID column)
{
    int32_t value = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               column,
                               &value,
                               sizeof(value),
                               &actualSize,
                               0,
                               nullptr));
    return value;
}

}  // namespace

EseIntegrationScenario(Escrow, IncrementOnce)
{
    TemporaryDirectory directory("Escrow.IncrementOnce");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Escrow.mdb");
    EseTable table(database, "Counter");

    const int32_t initialValue = 0;
    auto columnId = table.AddColumnWithDefault("Counter",
                                               JET_coltypLong,
                                               &initialValue,
                                               sizeof(initialValue),
                                               JET_bitColumnEscrowUpdate);

    InsertSingleEscrowRow(session, table);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    EseTransaction transaction(session);
    int32_t delta = 1;
    int32_t previousValue = -1;
    uint32_t actualSize = 0;
    CheckJet(JetEscrowUpdate(session.Handle(),
                             table.Id(),
                             columnId,
                             &delta,
                             sizeof(delta),
                             &previousValue,
                             sizeof(previousValue),
                             &actualSize,
                             0));
    Require(previousValue == initialValue);
    transaction.Commit();

    Require(RetrieveLong(session, table, columnId) == initialValue + delta);
}

EseIntegrationScenario(Escrow, IncrementManyTimesSumsCorrectly)
{
    TemporaryDirectory directory("Escrow.IncrementManyTimesSumsCorrectly");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Escrow.mdb");
    EseTable table(database, "Counter");

    const int32_t initialValue = 100;
    auto columnId = table.AddColumnWithDefault("Counter",
                                               JET_coltypLong,
                                               &initialValue,
                                               sizeof(initialValue),
                                               JET_bitColumnEscrowUpdate);

    InsertSingleEscrowRow(session, table);

    static constexpr int Increments = 50;
    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        for (int index = 0; index < Increments; ++index)
        {
            int32_t delta = 3;
            uint32_t actualSize = 0;
            CheckJet(JetEscrowUpdate(session.Handle(),
                                     table.Id(),
                                     columnId,
                                     &delta,
                                     sizeof(delta),
                                     nullptr,
                                     0,
                                     &actualSize,
                                     0));
        }
        transaction.Commit();
    }

    Require(RetrieveLong(session, table, columnId)
            == initialValue + Increments * 3);
}

EseIntegrationScenario(Escrow, NegativeDeltaDecrements)
{
    TemporaryDirectory directory("Escrow.NegativeDeltaDecrements");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Escrow.mdb");
    EseTable table(database, "Counter");

    const int32_t initialValue = 50;
    auto columnId = table.AddColumnWithDefault("Counter",
                                               JET_coltypLong,
                                               &initialValue,
                                               sizeof(initialValue),
                                               JET_bitColumnEscrowUpdate);

    InsertSingleEscrowRow(session, table);
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    {
        EseTransaction transaction(session);
        int32_t delta = -20;
        uint32_t actualSize = 0;
        CheckJet(JetEscrowUpdate(session.Handle(),
                                 table.Id(),
                                 columnId,
                                 &delta,
                                 sizeof(delta),
                                 nullptr,
                                 0,
                                 &actualSize,
                                 0));
        transaction.Commit();
    }

    Require(RetrieveLong(session, table, columnId) == 30);
}

EseIntegrationScenario(Escrow, EscrowRollbackDiscardsDelta)
{
    TemporaryDirectory directory("Escrow.EscrowRollbackDiscardsDelta");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Escrow.mdb");
    EseTable table(database, "Counter");

    const int32_t initialValue = 7;
    auto columnId = table.AddColumnWithDefault("Counter",
                                               JET_coltypLong,
                                               &initialValue,
                                               sizeof(initialValue),
                                               JET_bitColumnEscrowUpdate);

    InsertSingleEscrowRow(session, table);
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    {
        EseTransaction transaction(session);
        int32_t delta = 100;
        uint32_t actualSize = 0;
        CheckJet(JetEscrowUpdate(session.Handle(),
                                 table.Id(),
                                 columnId,
                                 &delta,
                                 sizeof(delta),
                                 nullptr,
                                 0,
                                 &actualSize,
                                 0));
        transaction.Rollback();
    }

    Require(RetrieveLong(session, table, columnId) == initialValue);
}

EseIntegrationScenario(Escrow, DeleteOnZeroEventuallyRemovesRecord)
{
    TemporaryDirectory directory("Escrow.DeleteOnZeroEventuallyRemovesRecord");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Escrow.mdb");
    EseTable table(database, "Refcounted");

    const int32_t initialValue = 2;
    auto columnId = table.AddColumnWithDefault(
        "RefCount",
        JET_coltypLong,
        &initialValue,
        sizeof(initialValue),
        JET_bitColumnEscrowUpdate | JET_bitColumnDeleteOnZero);

    InsertSingleEscrowRow(session, table);
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // Two decrements of 1 bring the counter to 0.
    {
        EseTransaction transaction(session);
        int32_t delta = -1;
        uint32_t actualSize = 0;
        CheckJet(JetEscrowUpdate(session.Handle(), table.Id(), columnId,
                                 &delta, sizeof(delta),
                                 nullptr, 0, &actualSize, 0));
        CheckJet(JetEscrowUpdate(session.Handle(), table.Id(), columnId,
                                 &delta, sizeof(delta),
                                 nullptr, 0, &actualSize, 0));
        transaction.Commit();
    }

    //  Deletion runs as a background task once the version store
    //  releases the row — JetIdle nudges but doesn't synchronously
    //  drain it.  Accept either of the two valid steady states:
    //  (a) row still present with counter==0 (deletion pending), or
    //  (b) row gone (deletion ran).  This is the engine contract for
    //  bitColumnDeleteOnZero — eventual, not immediate.
    CheckJet(JetIdle(session.Handle(),
                     JET_bitIdleWaitForAsyncActivity));
    const auto moveResult = JetMove(session.Handle(), table.Id(),
                                    JET_MoveFirst, 0);
    if (moveResult == JET_errNoCurrentRecord
        || moveResult == JET_errRecordDeleted)
    {
        //  Deleted — assert the table is still usable for a fresh
        //  insert (deletion didn't tombstone the B-tree).
        InsertSingleEscrowRow(session, table);
        CheckJet(JetMove(session.Handle(), table.Id(),
                         JET_MoveFirst, 0));
        Require(RetrieveLong(session, table, columnId) == initialValue);
    }
    else
    {
        //  Not deleted yet — counter must read exactly 0, which is
        //  the precondition for the eventual deletion.
        CheckJet(moveResult);
        Require(RetrieveLong(session, table, columnId) == 0);
    }
}

EseIntegrationScenario(Escrow, NoRollbackPreservesDeltaAcrossExplicitRollback)
{
    TemporaryDirectory directory(
        "Escrow.NoRollbackPreservesDeltaAcrossExplicitRollback");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Escrow.mdb");
    EseTable table(database, "Counter");

    const int32_t initialValue = 50;
    auto columnId = table.AddColumnWithDefault("Counter",
                                               JET_coltypLong,
                                               &initialValue,
                                               sizeof(initialValue),
                                               JET_bitColumnEscrowUpdate);

    InsertSingleEscrowRow(session, table);
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // JET_bitEscrowNoRollback tells the engine to skip the per-row
    // undo image for this escrow change.  A subsequent rollback
    // therefore CANNOT undo the delta — the counter stays bumped.
    {
        EseTransaction transaction(session);
        int32_t delta = 25;
        uint32_t actualSize = 0;
        CheckJet(JetEscrowUpdate(session.Handle(),
                                 table.Id(),
                                 columnId,
                                 &delta, sizeof(delta),
                                 nullptr, 0, &actualSize,
                                 JET_bitEscrowNoRollback));
        transaction.Rollback();
    }
    Require(RetrieveLong(session, table, columnId) == initialValue + 25);

    // Control: same operation WITHOUT the flag rolls back as normal —
    // the counter snaps back to 75 after a fresh +99 attempt is undone.
    {
        EseTransaction transaction(session);
        int32_t delta = 99;
        uint32_t actualSize = 0;
        CheckJet(JetEscrowUpdate(session.Handle(),
                                 table.Id(),
                                 columnId,
                                 &delta, sizeof(delta),
                                 nullptr, 0, &actualSize,
                                 0));
        transaction.Rollback();
    }
    Require(RetrieveLong(session, table, columnId) == initialValue + 25);

    // And a committed delta with NoRollback simply applies (the flag's
    // promise is "no undo" — committing keeps the change either way).
    {
        EseTransaction transaction(session);
        int32_t delta = 10;
        uint32_t actualSize = 0;
        CheckJet(JetEscrowUpdate(session.Handle(),
                                 table.Id(),
                                 columnId,
                                 &delta, sizeof(delta),
                                 nullptr, 0, &actualSize,
                                 JET_bitEscrowNoRollback));
        transaction.Commit();
    }
    Require(RetrieveLong(session, table, columnId) == initialValue + 25 + 10);
}
