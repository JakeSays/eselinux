// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/TemporaryDirectory.hxx"

EseIntegrationScenario(Transaction, BeginAndCommit)
{
    ese::tests::TemporaryDirectory directory("Transaction.BeginAndCommit");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "Transaction.mdb");

    ese::tests::EseTransaction transaction(session);
    transaction.Commit();
}
