// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// EseDatabase — RAII over JetCreateDatabase (or JetAttachDatabase +
// JetOpenDatabase). The database file is created inside the owning
// EseInstance's directory; the destructor closes and detaches it.

#pragma once

#include <jetapi.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace ese::tests
{

class EseSession;

enum class EseDatabaseMode
{
    Create,
    AttachAndOpen,
    // Open an already-attached database without performing a new
    // attach.  Use this for additional sessions when one session in
    // the same instance has already attached the file — avoids the
    // attach/detach churn that can race the engine's LV-tree page
    // flush under heavy concurrent writes.
    Open,
};

class EseDatabase
{
public:
    EseDatabase(EseSession& session,
                std::string_view filename,
                EseDatabaseMode mode = EseDatabaseMode::Create);
    ~EseDatabase();

    EseDatabase(const EseDatabase&) = delete;
    EseDatabase& operator=(const EseDatabase&) = delete;

    JET_DBID Id() const
    {
        return _id;
    }

    EseSession& Session() const
    {
        return _session;
    }

    const std::filesystem::path& Path() const
    {
        return _path;
    }

    // Detach the underlying JET handle from this wrapper.  After this
    // call the destructor becomes a no-op; the caller has assumed
    // ownership of the database file (e.g., to call
    // JetSetDatabaseSize, which needs exclusive access to a detached
    // database).
    void Release()
    {
        _id = JET_dbidNil;
        _path.clear();
    }

private:
    EseSession& _session;
    std::filesystem::path _path;
    JET_DBID _id = JET_dbidNil;
};

} // namespace ese::tests
