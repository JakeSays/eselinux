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

EseIntegrationScenario(DataManipulation, InsertAndReadBackOneRow)
{
    ese::tests::TemporaryDirectory directory("DataManipulation.InsertAndReadBackOneRow");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "Data.mdb");
    ese::tests::EseTable           table(database, "OneRow");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    columnDefinition.coltyp        = JET_coltypLong;
    columnDefinition.grbit         = JET_bitColumnNotNULL;

    JET_COLUMNID valueColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(),
                           table.Id(),
                           "Value",
                           &columnDefinition,
                           nullptr,
                           0,
                           &valueColumnId));

    ese::tests::EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));

    const int valueToInsert = 0x12345678;
    CheckJet(JetSetColumn(session.Handle(),
                          table.Id(),
                          valueColumnId,
                          &valueToInsert,
                          sizeof(valueToInsert),
                          0,
                          nullptr));
    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int      valueReadBack = 0;
    uint32_t actualSize    = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               valueColumnId,
                               &valueReadBack,
                               sizeof(valueReadBack),
                               &actualSize,
                               0,
                               nullptr));
    Require(actualSize == sizeof(valueReadBack));
    Require(valueReadBack == valueToInsert);
}
