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

EseIntegrationScenario(Navigation, MoveFirstNextLast)
{
    ese::tests::TemporaryDirectory directory("Navigation.MoveFirstNextLast");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "Navigation.mdb");
    ese::tests::EseTable           table(database, "Rows");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    columnDefinition.coltyp        = JET_coltypLong;
    columnDefinition.grbit         = JET_bitColumnAutoincrement;

    JET_COLUMNID identityColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(),
                           table.Id(),
                           "Identity",
                           &columnDefinition,
                           nullptr,
                           0,
                           &identityColumnId));

    ese::tests::EseTransaction transaction(session);
    for (int rowIndex = 0; rowIndex < 3; ++rowIndex)
    {
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    }
    transaction.Commit();

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int      firstIdentity = 0;
    uint32_t actualSize    = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               identityColumnId,
                               &firstIdentity,
                               sizeof(firstIdentity),
                               &actualSize,
                               0,
                               nullptr));

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveLast, 0));
    int lastIdentity = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               identityColumnId,
                               &lastIdentity,
                               sizeof(lastIdentity),
                               &actualSize,
                               0,
                               nullptr));
    Require(lastIdentity == firstIdentity + 2);
}
