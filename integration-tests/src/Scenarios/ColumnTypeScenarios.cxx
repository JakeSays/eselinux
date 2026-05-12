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

EseIntegrationScenario(ColumnType, LongColumnRoundTrip)
{
    ese::tests::TemporaryDirectory directory("ColumnType.LongColumnRoundTrip");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "ColumnType.mdb");
    ese::tests::EseTable           table(database, "LongValues");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    columnDefinition.coltyp        = JET_coltypLong;

    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(session.Handle(),
                           table.Id(),
                           "Value",
                           &columnDefinition,
                           nullptr,
                           0,
                           &columnId));

    ese::tests::EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
    const int written = -42;
    CheckJet(JetSetColumn(session.Handle(),
                          table.Id(),
                          columnId,
                          &written,
                          sizeof(written),
                          0,
                          nullptr));
    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int      readBack   = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               columnId,
                               &readBack,
                               sizeof(readBack),
                               &actualSize,
                               0,
                               nullptr));
    Require(actualSize == sizeof(readBack));
    Require(readBack == written);
}
