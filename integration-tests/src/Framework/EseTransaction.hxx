// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// EseTransaction — RAII over JetBeginTransaction → JetCommitTransaction
// (or JetRollback if not explicitly committed). Throws on
// double-commit; the destructor swallows rollback errors.

#pragma once

#include <jetapi.h>

namespace ese::tests
{

class EseSession;

class EseTransaction
{
public:
    explicit EseTransaction(EseSession& session,
                            JET_GRBIT beginFlags = 0);
    ~EseTransaction();

    EseTransaction(const EseTransaction&) = delete;
    EseTransaction& operator=(const EseTransaction&) = delete;

    // Commits the transaction. If commitFlags is 0, this is a regular
    // durable commit; pass JET_bitCommitLazyFlush for lazy.
    void Commit(JET_GRBIT commitFlags = 0);

    // Rolls back the transaction explicitly; no-op if already finished.
    void Rollback();

    EseSession& Session() const
    {
        return _session;
    }

private:
    EseSession& _session;
    bool _active = false;
};

} // namespace ese::tests
