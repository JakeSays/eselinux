// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
//  EseDatabase — RAII over JetCreateDatabase (or JetAttachDatabase +
//  JetOpenDatabase).  The database file is created inside the owning
//  EseInstance's directory; the destructor closes and detaches it.

#pragma once

#include <jet.h>

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
};

class EseDatabase
{
public:
    EseDatabase(EseSession&      session,
                std::string_view filename,
                EseDatabaseMode  mode = EseDatabaseMode::Create);
    ~EseDatabase();

    EseDatabase(const EseDatabase&)            = delete;
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

private:
    EseSession&           _session;
    std::filesystem::path _path;
    JET_DBID              _id = JET_dbidNil;
};

}  //  namespace ese::tests
