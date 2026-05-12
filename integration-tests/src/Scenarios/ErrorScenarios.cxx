// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

EseIntegrationScenario(Error, RetrievingUnknownColumnReturnsColumnNotFound)
{
    ese::tests::TemporaryDirectory directory(
        "Error.RetrievingUnknownColumnReturnsColumnNotFound");
    ese::tests::EseInstance instance(directory);
    ese::tests::EseSession  session(instance);
    ese::tests::EseDatabase database(session, "Error.mdb");
    ese::tests::EseTable    table(database, "Empty");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    RequireJetError(JetGetColumnInfoA(session.Handle(),
                                      database.Id(),
                                      "Empty",
                                      "NoSuchColumn",
                                      &columnDefinition,
                                      sizeof(columnDefinition),
                                      JET_ColInfo),
                    JET_errColumnNotFound);
}
