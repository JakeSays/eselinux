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

// SingleInstance — set system params globally, then JetInit.  ESE
// stays in single-instance mode; the no-instance "Global" APIs
// (JetBeginExternalBackup, JetTruncateLog, etc.) work.  This is the
// default and matches almost every scenario in the suite.
//
// MultiInstance — JetCreateInstance2, then set per-instance params,
// then JetInit.  The engine permanently switches to multi-instance
// mode for the lifetime of the process.  Required when a scenario
// needs more than one live JET_INSTANCE simultaneously, but it
// breaks the no-instance Global APIs from that point onward — so
// scenarios that opt in should run in isolation (or be ordered last).
enum class EseInstanceMode
{
    SingleInstance,
    MultiInstance,
};

//  Optional knobs that diverge from the framework defaults.  Scenarios
//  that need a non-default engine configuration construct one of these,
//  set the bits they want to flip, and pass it to EseInstance — same
//  pattern as Win32 attribute structs.  Adding a new knob doesn't
//  touch any of the existing call sites.
struct EseInstanceOptions
{
    //  Circular logging is on by default so the framework keeps log
    //  directories bounded across the suite.  Backup-family scenarios
    //  (incremental/atomic/surrogate) need it off because the engine
    //  rejects those backups with JET_errInvalidBackup under CircularLog.
    bool EnableCircularLog = true;
};

class EseInstance
{
public:
    // Constructs an instance whose system path, log path, and temp path
    // all live under directory.Path(). instanceName is used as
    // JET_paramBaseName so multiple instances under the same parent
    // directory don't collide.
    //
    // runtimeCallback wires JET_paramRuntimeCallback before JetInit —
    // required for JetSetLS / JetGetLS, JetRegisterCallback for the
    // free-LS callback types, and the user-defined-default codepath.
    // Pass nullptr (the default) when the scenario doesn't need it.
    EseInstance(const TemporaryDirectory& directory,
                std::string_view instanceName = "ese-tests",
                JET_CALLBACK runtimeCallback = nullptr,
                EseInstanceMode mode = EseInstanceMode::SingleInstance,
                EseInstanceOptions options = {});
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
