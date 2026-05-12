// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

EseIntegrationScenario(TemporaryTable, OpenSortedTempTable)
{
    TemporaryDirectory directory("TemporaryTable.OpenSortedTempTable");
    EseInstance        instance(directory);
    EseSession         session(instance);

    JET_COLUMNDEF columnDefinitions[1] = { {} };
    columnDefinitions[0].cbStruct      = sizeof(columnDefinitions[0]);
    columnDefinitions[0].coltyp        = JET_coltypLong;
    columnDefinitions[0].grbit         = JET_bitColumnTTKey;

    JET_COLUMNID columnIdentifiers[1] = { 0 };
    JET_TABLEID  temporaryTableId     = JET_tableidNil;
    CheckJet(JetOpenTempTable(session.Handle(),
                              columnDefinitions,
                              1,
                              JET_bitTTUpdatable,
                              &temporaryTableId,
                              columnIdentifiers));
    Require(temporaryTableId != JET_tableidNil);
    CheckJet(JetCloseTable(session.Handle(), temporaryTableId));
}
