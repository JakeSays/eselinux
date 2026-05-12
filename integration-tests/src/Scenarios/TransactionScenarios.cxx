// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

EseIntegrationScenario(Transaction, BeginAndCommit)
{
    TemporaryDirectory directory("Transaction.BeginAndCommit");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Transaction.mdb");

    EseTransaction transaction(session);
    transaction.Commit();
}
