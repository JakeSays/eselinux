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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace ese::tests
{

// A child-entry function receives its scratch directory and any extra
// argv tokens the parent appended via ChildProcess.  Per-child state
// (TCP ports, role tags, peer endpoints) travels through `extraArgs`
// — never via on-disk config files.  Use a simple `key=value`
// convention so each token is self-describing and order-independent.
using ChildEntryPoint = std::function<void(
    const std::filesystem::path& directory,
    std::span<const std::string_view> extraArgs)>;

// Convenience alias for child entries that don't care about extra
// args — most recovery-style scenarios.  Register one of these with
// the 1-arg overload of RegisterChildEntry below.
using SimpleChildEntryPoint =
    std::function<void(const std::filesystem::path& directory)>;

// Register a child-process entry point. The name must match the
// --child-entry argument the parent passes to itself.
void RegisterChildEntry(std::string name, ChildEntryPoint entry);

// Overload for child entries that take only a directory.  The
// implementation wraps the callback so the underlying registry
// still stores a uniform 2-arg signature; extra args, if any, are
// silently dropped.
void RegisterChildEntry(std::string name, SimpleChildEntryPoint entry);

// Look up a previously-registered entry by name. Used by Main()
// when invoked with --child-entry. Returns nullptr if unknown.
const ChildEntryPoint* FindChildEntry(std::string_view name);

class ChildProcess
{
public:
    // Forks and re-executes the current binary with
    // --child-entry <entryName> --child-directory <directory>
    // followed by every token in `extraArgs`.  The child entry looks
    // up the entry by name and invokes it with the directory and
    // extraArgs view.
    ChildProcess(std::string_view entryName,
                 const std::filesystem::path& directory,
                 std::span<const std::string> extraArgs = {});
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
