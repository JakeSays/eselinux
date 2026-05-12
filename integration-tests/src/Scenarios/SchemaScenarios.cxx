// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

EseIntegrationScenario(Schema, CreateTable)
{
    ese::tests::TemporaryDirectory directory("Schema.CreateTable");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "Schema.mdb");
    ese::tests::EseTable           table(database, "Customers");

    Require(table.Id() != JET_tableidNil);
}
