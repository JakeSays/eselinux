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

EseIntegrationScenario(Platform, CreateInstanceAllocatesHandleWithoutInit)
{
    //  JetCreateInstance is the unversioned alloc-only form (the
    //  framework uses JetCreateInstance2 elsewhere because it accepts
    //  a display name).  Round-trip Create -> Term without an Init in
    //  between is the documented "abandon" path.
    JET_INSTANCE handle = JET_instanceNil;
    CheckJet(JetCreateInstanceA(&handle, "Platform.CreateInstance"));
    Require(handle != JET_instanceNil);

    CheckJet(JetTerm(handle));
}
