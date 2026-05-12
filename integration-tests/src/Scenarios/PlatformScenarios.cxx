// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

EseIntegrationScenario(Platform, InitializeAndTerminate)
{
    TemporaryDirectory directory("Platform.InitializeAndTerminate");
    EseInstance instance(directory);
    // Instance dtor calls JetTerm; if either init or term fails the
    // RAII wrapper throws and the scenario fails.
}
