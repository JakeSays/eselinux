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

EseIntegrationScenario(Maintenance, ComputeStatsOnEmptyTable)
{
    TemporaryDirectory directory("Maintenance.ComputeStatsOnEmptyTable");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Maintenance.mdb");
    EseTable           table(database, "Empty");

    CheckJet(JetComputeStats(session.Handle(), table.Id()));
}
