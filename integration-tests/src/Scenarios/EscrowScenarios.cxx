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

EseIntegrationScenario(Escrow, IncrementOnce)
{
    TemporaryDirectory directory("Escrow.IncrementOnce");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Escrow.mdb");
    EseTable           table(database, "Counter");

    const int initialValue = 0;

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    columnDefinition.coltyp        = JET_coltypLong;
    columnDefinition.grbit         = JET_bitColumnEscrowUpdate;

    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(session.Handle(),
                           table.Id(),
                           "Counter",
                           &columnDefinition,
                           &initialValue,
                           sizeof(initialValue),
                           &columnId));

    EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    EseTransaction updateTransaction(session);
    const int delta         = 1;
    int       previousValue = -1;
    uint32_t  actualSize    = 0;
    CheckJet(JetEscrowUpdate(session.Handle(),
                             table.Id(),
                             columnId,
                             const_cast<int*>(&delta),
                             sizeof(delta),
                             &previousValue,
                             sizeof(previousValue),
                             &actualSize,
                             0));
    Require(previousValue == initialValue);
    updateTransaction.Commit();
}
