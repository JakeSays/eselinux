// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Replication orchestrator — parent-side helpers for round-8 scenarios.
// Three responsibilities:
//
//   1. Seed an empty ESE database in a "template" directory and clone
//      it into per-replica scratch directories so every replica starts
//      with the same JET_SIGNATURE in its .edb header.  Without a
//      matching signature the passive engine rejects log data from
//      the active.
//
//   2. (Future scenarios) any other pre-fork file-tree setup the
//      parent needs before handing off to the active.
//
//   3. Reap a spawned child and require its exit status to be 0
//      (clean shutdown).  Throws if the child exited non-zero or was
//      killed by a signal.
//
// Port discovery and peer-endpoint plumbing live entirely on the
// command line: the parent forks the active, the active opens its own
// TCP listener and fork+execs the passive with --connect-port=N as a
// --child-arg.  No port files, no readiness sentinels for port info.

#pragma once

#include "Framework/CrashHelper.hxx"

#include <filesystem>
#include <string_view>

namespace ese::tests::replication
{

// Create a fresh empty database (one Long column "Value" in a table
// called "Rows") in `seedDirectory` under the file name
// `databaseFileName`.  Leaves the .edb / log / checkpoint files on
// disk so they can be cloned into each replica's directory.  The
// seed engine is fully JetTerm'd before this returns; subsequent
// JetInit calls on the cloned directories pick up a clean, consistent
// set of files.
void SeedDatabaseInto(const std::filesystem::path& seedDirectory,
                      const char* databaseFileName);

// Recursively copy every regular file from `sourceDirectory` into
// `destinationDirectory`.  Used after SeedDatabaseInto to clone the
// seed into per-replica scratch dirs.
void CloneDirectory(const std::filesystem::path& sourceDirectory,
                    const std::filesystem::path& destinationDirectory);

// Wait for `child` to exit and require its exit status to be 0.
// Throws if the child exited non-zero, was killed by a signal, or
// otherwise terminated abnormally.
void RequireCleanExit(ChildProcess& child, std::string_view childLabel);

} // namespace ese::tests::replication
