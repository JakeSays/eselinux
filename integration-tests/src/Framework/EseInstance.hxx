// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// EseInstance — RAII over JetCreateInstance2 → JetInit → JetTerm.
// Sets SystemPath, LogFilePath, TempPath, BaseName, EventSource, and
// CircularLog so each scenario gets its own database/log directory.
//
// Caveat: ESE keeps process-global state that survives JetTerm — the
// resource manager freezes its buffer pool / cursor table / cache
// parameters on first commit, and they stay frozen for the lifetime
// of the process. A fresh EseInstance is therefore *not* a fresh
// engine. Scenarios that need a pristine engine (recovery, parameter
// re-matrixing) must run in a fork()'d child via CrashHelper.

#pragma once

#include <jetapi.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace ese::tests
{

class TemporaryDirectory;

class EseInstance
{
public:
    // Constructs an instance whose system path, log path, and temp path
    // all live under directory.Path(). instanceName is used as
    // JET_paramBaseName so multiple instances under the same parent
    // directory don't collide.
    EseInstance(const TemporaryDirectory& directory,
                std::string_view instanceName = "ese-tests");
    ~EseInstance();

    EseInstance(const EseInstance&) = delete;
    EseInstance& operator=(const EseInstance&) = delete;

    JET_INSTANCE Handle() const
    {
        return _handle;
    }

    const std::filesystem::path& Directory() const
    {
        return _directory;
    }

private:
    JET_INSTANCE _handle = JET_instanceNil;
    std::filesystem::path _directory;
};

} // namespace ese::tests
