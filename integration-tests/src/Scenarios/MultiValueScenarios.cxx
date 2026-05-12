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

EseIntegrationScenario(MultiValue, TwoItagsRoundTrip)
{
    ese::tests::TemporaryDirectory directory("MultiValue.TwoItagsRoundTrip");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "MultiValue.mdb");
    ese::tests::EseTable           table(database, "Tagged");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    columnDefinition.coltyp        = JET_coltypLong;
    columnDefinition.grbit         = JET_bitColumnTagged | JET_bitColumnMultiValued;

    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(session.Handle(),
                           table.Id(),
                           "Tags",
                           &columnDefinition,
                           nullptr,
                           0,
                           &columnId));

    ese::tests::EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));

    const int firstValue  = 101;
    const int secondValue = 202;

    JET_SETINFO setInformation = {};
    setInformation.cbStruct    = sizeof(setInformation);

    setInformation.itagSequence = 1;
    CheckJet(JetSetColumn(session.Handle(),
                          table.Id(),
                          columnId,
                          &firstValue,
                          sizeof(firstValue),
                          0,
                          &setInformation));

    setInformation.itagSequence = 2;
    CheckJet(JetSetColumn(session.Handle(),
                          table.Id(),
                          columnId,
                          &secondValue,
                          sizeof(secondValue),
                          0,
                          &setInformation));

    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    JET_RETINFO retrieveInformation = {};
    retrieveInformation.cbStruct    = sizeof(retrieveInformation);
    int      readBack   = 0;
    uint32_t actualSize = 0;

    retrieveInformation.itagSequence = 1;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               columnId,
                               &readBack,
                               sizeof(readBack),
                               &actualSize,
                               0,
                               &retrieveInformation));
    Require(readBack == firstValue);

    retrieveInformation.itagSequence = 2;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               columnId,
                               &readBack,
                               sizeof(readBack),
                               &actualSize,
                               0,
                               &retrieveInformation));
    Require(readBack == secondValue);
}
