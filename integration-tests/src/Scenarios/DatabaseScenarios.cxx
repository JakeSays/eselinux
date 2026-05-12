// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <filesystem>

EseIntegrationScenario(Database, CreateAndClose)
{
    ese::tests::TemporaryDirectory directory("Database.CreateAndClose");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "Smoke.mdb");

    Require(database.Id() != JET_dbidNil);
    Require(std::filesystem::exists(database.Path()));
}
