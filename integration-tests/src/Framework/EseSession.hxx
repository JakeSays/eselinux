// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
//  EseSession — RAII over JetBeginSession → JetEndSession.  A session
//  is the unit of transaction scope; one session per thread.

#pragma once

#include <jet.h>

namespace ese::tests
{

class EseInstance;

class EseSession
{
public:
    explicit EseSession(EseInstance& instance);
    ~EseSession();

    EseSession(const EseSession&)            = delete;
    EseSession& operator=(const EseSession&) = delete;

    JET_SESID Handle() const
    {
        return _handle;
    }

    EseInstance& Instance() const
    {
        return _instance;
    }

private:
    EseInstance& _instance;
    JET_SESID    _handle = JET_sesidNil;
};

}  //  namespace ese::tests
