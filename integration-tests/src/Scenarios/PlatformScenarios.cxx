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

EseIntegrationScenario(Platform, GetInstanceInfoEnumeratesRunningInstance)
{
    TemporaryDirectory directory(
        "Platform.GetInstanceInfoEnumeratesRunningInstance");
    EseInstance instance(directory);

    // JetGetInstanceInfo is instance-list-style: the engine allocates
    // an array of JET_INSTANCE_INFO_A entries via JetMalloc.  Caller
    // releases via JetFreeBuffer.
    uint32_t instanceCount = 0;
    JET_INSTANCE_INFO_A* instanceInfoArray = nullptr;
    CheckJet(JetGetInstanceInfoA(&instanceCount, &instanceInfoArray));
    // Our running instance must show up.
    Require(instanceCount >= 1);
    Require(instanceInfoArray != nullptr);
    CheckJet(JetFreeBuffer(reinterpret_cast<char*>(instanceInfoArray)));
}
