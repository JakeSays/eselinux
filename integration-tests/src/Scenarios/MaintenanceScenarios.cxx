// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

EseIntegrationScenario(Maintenance, ComputeStatsOnEmptyTable)
{
    ese::tests::TemporaryDirectory directory("Maintenance.ComputeStatsOnEmptyTable");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "Maintenance.mdb");
    ese::tests::EseTable           table(database, "Empty");

    CheckJet(JetComputeStats(session.Handle(), table.Id()));
}
