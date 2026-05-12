// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <filesystem>

using namespace ese::tests;

EseIntegrationScenario(Database, CreateAndClose)
{
    TemporaryDirectory directory("Database.CreateAndClose");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Smoke.mdb");

    Require(database.Id() != JET_dbidNil);
    Require(std::filesystem::exists(database.Path()));
}
