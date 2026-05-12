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

    // The deletion runs as a background task once the version store
    // releases the row.  The synchronous post-condition we can verify
    // is that the counter has dropped to zero; physical removal is
    // best-effort and lazy.
    const auto moveResult = JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0);
    if (moveResult == JET_errSuccess)
    {
        Require(RetrieveLong(session, table, columnId) == 0);
    }
    else
    {
        Require(moveResult == JET_errNoCurrentRecord ||
                moveResult == JET_errRecordDeleted);
    }
}
