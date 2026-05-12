// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/EseTransaction.hxx"

#include "Framework/Check.hxx"
#include "Framework/EseSession.hxx"

namespace ese::tests
{

EseTransaction::EseTransaction(EseSession& session,
                               JET_GRBIT beginFlags)
    : _session(session)
{
    CheckJet(JetBeginTransaction2(_session.Handle(), beginFlags));
    _active = true;
}

EseTransaction::~EseTransaction()
{
    if (_active)
    {
        // Best-effort rollback in the dtor — if the scenario didn't
        // commit, that's intentional or a leaked transaction; either
        // way we want the engine state clean for the next scenario.
        (void)JetRollback(_session.Handle(), 0);
        _active = false;
    }
}

void EseTransaction::Commit(JET_GRBIT commitFlags)
{
    if (!_active)
    {
        return;
    }
    CheckJet(JetCommitTransaction(_session.Handle(), commitFlags));
    _active = false;
}

void EseTransaction::Rollback()
{
    if (!_active)
    {
        return;
    }
    CheckJet(JetRollback(_session.Handle(), 0));
    _active = false;
}

} // namespace ese::tests
