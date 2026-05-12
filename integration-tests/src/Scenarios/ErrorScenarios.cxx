// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

EseIntegrationScenario(Error, RetrievingUnknownColumnReturnsColumnNotFound)
{
    TemporaryDirectory directory(
        "Error.RetrievingUnknownColumnReturnsColumnNotFound");
    EseInstance instance(directory);
    EseSession  session(instance);
    EseDatabase database(session, "Error.mdb");
    EseTable    table(database, "Empty");

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
