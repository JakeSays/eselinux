// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/EseDatabase.hxx"

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"

namespace ese::tests
{

EseDatabase::EseDatabase(EseSession& session,
                         std::string_view filename,
                         EseDatabaseMode mode)
    : _session(session),
      _path(session.Instance().Directory() / std::string(filename))
{
    const auto pathString = _path.string();

    switch (mode)
    {
        case EseDatabaseMode::Create:
        {
            CheckJet(JetCreateDatabaseA(_session.Handle(),
                                        pathString.c_str(),
                                        nullptr,
                                        &_id,
                                        JET_bitDbOverwriteExisting));
            break;
        }
        case EseDatabaseMode::AttachAndOpen:
        {
            CheckJet(JetAttachDatabaseA(_session.Handle(),
                                        pathString.c_str(),
                                        0));
            CheckJet(JetOpenDatabaseA(_session.Handle(),
                                      pathString.c_str(),
                                      nullptr,
                                      &_id,
                                      0));
            break;
        }
    }
}

EseDatabase::~EseDatabase()
{
    if (_id != JET_dbidNil)
    {
        (void)JetCloseDatabase(_session.Handle(), _id, 0);
        _id = JET_dbidNil;
    }
    // Detach if we still own the attachment. JetCreateDatabase keeps
    // the database attached after the close, so detach in both modes.
    (void)JetDetachDatabaseA(_session.Handle(), _path.string().c_str());
}

} // namespace ese::tests
