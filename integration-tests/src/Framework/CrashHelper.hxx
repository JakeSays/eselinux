// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// CrashHelper — fork + SIGKILL machinery for recovery scenarios.
//
// A recovery test wants to: spin up ESE in a child process, drive it
// to a known transaction state, kill -9 the child, and then verify the
// recovered database state from the parent. This header provides a
// child-entry registry and a ChildProcess RAII wrapper to do that.
//
// Usage sketch:
//
// RegisterChildEntry("Recovery.LooseTransaction",
// [](const fs::path& directory) { ... });
//
// ChildProcess child("Recovery.LooseTransaction", directory);
// child.WaitUntilReady(std::chrono::seconds(5));
// child.Kill();
// child.WaitForExit();
// // reopen the database and assert recovered state

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include <sys/types.h>

namespace ese::tests
{

using ChildEntryPoint = std::function<void(const std::filesystem::path& directory)>;

// Register a child-process entry point. The name must match the
// --child-entry argument the parent passes to itself.
void RegisterChildEntry(std::string name, ChildEntryPoint entry);

// Look up a previously-registered entry by name. Used by Main()
// when invoked with --child-entry. Returns nullptr if unknown.
const ChildEntryPoint* FindChildEntry(std::string_view name);

class ChildProcess
{
public:
    // Forks and re-executes the current binary with
    // --child-entry <entryName> --child-directory <directory>. The
    // child looks up the entry by name and invokes it.
    ChildProcess(std::string_view entryName,
                 const std::filesystem::path& directory);
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Waits until the child writes a sentinel file inside its directory
    // signalling that the engine is open and the test state is set.
    // Throws if the timeout elapses first.
    void WaitUntilReady(std::chrono::milliseconds timeout);

    // Sends SIGKILL. The child has no chance to clean up.
    void Kill();

    // Reaps the child. Returns the exit status (wait4-style).
    int WaitForExit();

    // Children call this to write the "ready" sentinel.
    static void SignalReady(const std::filesystem::path& directory);

    pid_t Pid() const
    {
        return _pid;
    }

private:
    pid_t _pid = -1;
    std::filesystem::path _directory;
    bool _reaped = false;
};

} // namespace ese::tests
