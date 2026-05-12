// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

EseIntegrationScenario(Session, OpenAndClose)
{
    ese::tests::TemporaryDirectory directory("Session.OpenAndClose");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    Require(session.Handle() != JET_sesidNil);
}
