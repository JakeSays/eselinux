// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

EseIntegrationScenario(Platform, InitializeAndTerminate)
{
    ese::tests::TemporaryDirectory directory("Platform.InitializeAndTerminate");
    ese::tests::EseInstance        instance(directory);
    //  Instance dtor calls JetTerm; if either init or term fails the
    //  RAII wrapper throws and the scenario fails.
}
