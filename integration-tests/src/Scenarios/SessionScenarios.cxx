// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

EseIntegrationScenario(Session, OpenAndClose)
{
    TemporaryDirectory directory("Session.OpenAndClose");
    EseInstance        instance(directory);
    EseSession         session(instance);
    Require(session.Handle() != JET_sesidNil);
}
