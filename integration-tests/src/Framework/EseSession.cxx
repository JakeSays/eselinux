// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/EseSession.hxx"

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"

namespace ese::tests
{

EseSession::EseSession(EseInstance& instance)
    : _instance(instance)
{
    CheckJet(JetBeginSessionA(_instance.Handle(),
                              &_handle,
                              nullptr,
                              nullptr));
}

EseSession::~EseSession()
{
    if (_handle != JET_sesidNil)
    {
        (void)JetEndSession(_handle, 0);
        _handle = JET_sesidNil;
    }
}

} // namespace ese::tests
