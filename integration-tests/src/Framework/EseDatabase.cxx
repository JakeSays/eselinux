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
        case EseDatabaseMode::Open:
        {
            // Skip JetAttachDatabase — caller has guaranteed another
            // session in the same instance keeps the file attached.
            // _path stays cleared so the destructor doesn't detach.
            CheckJet(JetOpenDatabaseA(_session.Handle(),
                                      pathString.c_str(),
                                      nullptr,
                                      &_id,
                                      0));
            _path.clear();
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
    // Detach only when we still own a path.  Released wrappers and
    // EseDatabaseMode::Open both clear _path so the engine-level
    // attachment is left for the owning session to tear down — that
    // session's own destructor (or the EseInstance's JetTerm2) does
    // the actual detach.  Without this, two sessions racing detach
    // calls against the same file can leave the engine's LV-tree
    // buffers in an inconsistent state.
    if (!_path.empty())
    {
        (void)JetDetachDatabaseA(_session.Handle(),
                                  _path.string().c_str());
        _path.clear();
    }
}

} // namespace ese::tests
