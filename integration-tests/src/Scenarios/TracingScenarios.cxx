// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Tracing scenarios exercise the LTTng UST analytic-tracing path end to
// end: libese.so dlopens libese_tracepoints.so, which registers the
// "ese:*" userspace tracepoint provider; an ESE workload then emits
// analytic events that an out-of-process LTTng session captures. The
// scenario drives the session with the lttng CLI, runs the workload in a
// fresh child process (so the provider registers with the session daemon
// after the session is live), stops the session, then decodes the CTF
// trace with babeltrace and asserts on the events that were captured.
//
// These require lttng-tools + babeltrace on the box, plus the staged
// libese_tracepoints.so next to libese.so (the ese-tests target depends
// on package-libs, which stages it). A box built without lttng-ust has
// no provider .so, so the capture comes back empty and the scenario
// fails loudly — that is the correct signal that tracing isn't wired,
// not a reason to skip the test.

#include "Framework/Check.hxx"
#include "Framework/CrashHelper.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <jetapi.h>

#include <array>
#include <filesystem>
#include <format>
#include <set>
#include <span>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string_view>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

using namespace ese::tests;

namespace
{

namespace fs = std::filesystem;

constexpr const char* ChildEntryAnalyticWorkload = "Tracing.AnalyticWorkload";

// Run a shell command, capturing stdout+stderr together. Returns the
// combined output; writes the process exit code (or -1 on spawn failure)
// into exitCode.
std::string RunShellCommand(const std::string& command, int& exitCode)
{
    std::string output;
    auto fullCommand = command + " 2>&1";
    auto* pipe = popen(fullCommand.c_str(), "r");
    if (pipe == nullptr)
    {
        exitCode = -1;
        return output;
    }
    std::array<char, 4096> buffer;
    size_t bytesRead = 0;
    while ((bytesRead = fread(buffer.data(), 1, buffer.size(), pipe)) > 0)
    {
        output.append(buffer.data(), bytesRead);
    }
    auto status = pclose(pipe);
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return output;
}

// Run a command and throw ScenarioFailure (carrying the command and its
// output) if it exits non-zero. Returns the captured output on success.
std::string RunCommandOrThrow(const std::string& command)
{
    int exitCode = 0;
    auto output = RunShellCommand(command, exitCode);
    if (exitCode != 0)
    {
        throw ScenarioFailure(std::format("command failed (exit {}): {}\n{}",
                                          exitCode, command, output));
    }
    return output;
}

// RAII over an LTTng userspace tracing session targeting ese:* events.
// The session is created and the events enabled in the constructor;
// Start and Stop bracket the workload; the destructor always destroys
// the session so an aborted run doesn't leak session-daemon state.
class LttngSession
{
public:
    LttngSession(std::string sessionName, const fs::path& outputDirectory,
                 std::string_view eventSpec)
        : _name(std::move(sessionName))
    {
        // Clear any stale session left behind by a previous aborted run.
        int ignoredExitCode = 0;
        RunShellCommand(std::format("lttng destroy {}", _name), ignoredExitCode);

        RunCommandOrThrow(std::format("lttng create {} --output \"{}\"",
                                      _name, outputDirectory.string()));
        _created = true;
        // eventSpec is a comma-separated lttng userspace event list, e.g.
        // "ese:*" for everything or "ese:TransactionCommit,ese:CacheNewPage"
        // for a selective enable.  Single-quoted so the shell doesn't glob
        // the '*'.
        RunCommandOrThrow(std::format("lttng enable-event --session {} "
                                      "--userspace '{}'",
                                      _name, eventSpec));
    }

    ~LttngSession()
    {
        if (_created)
        {
            int ignoredExitCode = 0;
            RunShellCommand(std::format("lttng destroy {}", _name), ignoredExitCode);
        }
    }

    LttngSession(const LttngSession&) = delete;
    LttngSession& operator=(const LttngSession&) = delete;

    void Start()
    {
        RunCommandOrThrow(std::format("lttng start {}", _name));
    }

    void Stop()
    {
        RunCommandOrThrow(std::format("lttng stop {}", _name));
    }

private:
    std::string _name;
    bool _created = false;
};

// The decoded result of an ese:* capture: how many events, and the set
// of distinct event-type names (the identifier after "ese:").
struct TraceSummary
{
    uint64_t totalEvents = 0;
    std::set<std::string> distinctTypes;
};

// Scan babeltrace's textual CTF dump for "ese:<Name>" event markers and
// tally totals + distinct types.
TraceSummary SummarizeEseEvents(const std::string& babeltraceOutput)
{
    TraceSummary summary;
    constexpr std::string_view marker = "ese:";
    size_t position = 0;
    while (true)
    {
        position = babeltraceOutput.find(marker, position);
        if (position == std::string::npos)
        {
            break;
        }
        auto nameStart = position + marker.size();
        auto nameEnd = nameStart;
        while (nameEnd < babeltraceOutput.size())
        {
            auto character = babeltraceOutput[nameEnd];
            auto isIdentifier = (character >= 'A' && character <= 'Z')
                || (character >= 'a' && character <= 'z')
                || (character >= '0' && character <= '9')
                || character == '_';
            if (!isIdentifier)
            {
                break;
            }
            ++nameEnd;
        }
        if (nameEnd > nameStart)
        {
            summary.totalEvents += 1;
            summary.distinctTypes.insert(
                babeltraceOutput.substr(nameStart, nameEnd - nameStart));
        }
        position = nameEnd;
    }
    return summary;
}

// Workload child: opens a fresh engine, builds a small schema, then
// commits several transactions of inserts (plus one rollback and a
// read-back pass) so the analytic path emits transaction, cache, space,
// IO, and log tracepoints. Runs in its own process so the UST provider
// registers with the session daemon after the session is already live.
void RunAnalyticWorkload(const fs::path& /*directory*/,
                         std::span<const std::string_view> /*extraArgs*/)
{
    TemporaryDirectory scratch("Tracing.AnalyticWorkload");
    EseInstance instance(scratch, "ese-tests-trace");
    EseSession session(instance);
    EseDatabase database(session, "Trace.mdb");
    EseTable table(database, "Events");

    auto valueColumn = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);
    auto payloadColumn = table.AddColumn("Payload", JET_coltypLongBinary);

    constexpr int32_t TransactionCount = 8;
    constexpr int32_t RowsPerTransaction = 64;
    const std::vector<uint8_t> payload(512, 0xCD);

    int32_t value = 0;
    for (int32_t transactionIndex = 0; transactionIndex < TransactionCount; ++transactionIndex)
    {
        EseTransaction transaction(session);
        for (int32_t rowIndex = 0; rowIndex < RowsPerTransaction; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), valueColumn,
                                  &value, sizeof(value), 0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), payloadColumn,
                                  payload.data(),
                                  static_cast<uint32_t>(payload.size()),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
            ++value;
        }
        transaction.Commit();
    }

    // One rolled-back transaction so the rollback path is exercised too.
    {
        EseTransaction rolledBack(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), valueColumn,
                              &value, sizeof(value), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        rolledBack.Rollback();
    }

    // Read-back pass to drive page reads and cache traffic.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    while (true)
    {
        int32_t readValue = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), valueColumn,
                                   &readValue, sizeof(readValue), &actualBytes,
                                   0, nullptr));
        auto moveResult = JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
}

struct AnalyticWorkloadRegistrar
{
    AnalyticWorkloadRegistrar()
    {
        RegisterChildEntry(ChildEntryAnalyticWorkload, &RunAnalyticWorkload);
    }
};
[[maybe_unused]] static AnalyticWorkloadRegistrar _analyticWorkloadRegistrar;

// Assert a specific ese:<eventType> is NOT in the capture — used by the
// selective-enable scenario to prove the per-event gate is selective.
void RequireEventTypeAbsent(const TraceSummary& summary,
                            std::string_view eventType)
{
    if (!summary.distinctTypes.contains(std::string(eventType)))
    {
        return;
    }
    throw ScenarioFailure(std::format(
        "ese:{} was captured but it was never enabled — the per-event gate "
        "let through an event it should have suppressed",
        eventType));
}

// Capture a trace around an out-of-process workload and return the
// decoded summary. eventSpec selects which ese:* events the session
// enables (default: all). Shared by the scenarios below.
TraceSummary CaptureWorkloadTrace(const TemporaryDirectory& directory,
                                  std::string_view eventSpec = "ese:*")
{
    auto traceDirectory = directory.Path() / "trace";
    auto workloadDirectory = directory.Path() / "workload";
    fs::create_directories(traceDirectory);
    fs::create_directories(workloadDirectory);

    LttngSession session(std::format("ese-tests-trace-{}", ::getpid()),
                         traceDirectory, eventSpec);
    session.Start();

    ChildProcess workload(ChildEntryAnalyticWorkload, workloadDirectory);
    auto exitStatus = workload.WaitForExit();
    Require(WIFEXITED(exitStatus) && WEXITSTATUS(exitStatus) == 0);

    session.Stop();

    auto babeltraceOutput =
        RunCommandOrThrow(std::format("babeltrace \"{}\"", traceDirectory.string()));
    return SummarizeEseEvents(babeltraceOutput);
}

// Assert a specific ese:<eventType> appears in the capture. On a miss,
// list the types that were captured so the failure is diagnosable.
void RequireEventTypePresent(const TraceSummary& summary,
                             std::string_view eventType)
{
    if (summary.distinctTypes.contains(std::string(eventType)))
    {
        return;
    }
    std::string captured;
    for (const auto& type : summary.distinctTypes)
    {
        if (!captured.empty())
        {
            captured += ", ";
        }
        captured += type;
    }
    throw ScenarioFailure(std::format(
        "expected ese:{} in the capture but it was absent; captured types: {}",
        eventType, captured));
}

} // namespace

// Baseline: a real workload must surface as a substantial, diverse set
// of analytic events through the whole emit -> provider -> UST -> CTF
// pipeline.  512 inserts across 8 transactions emit thousands of events
// spanning many subsystems; require a healthy floor and several distinct
// types so a single stray event can't satisfy the assertion.
EseIntegrationScenario(Tracing, AnalyticEventsCapturedDuringWorkload, Regression)
{
    TemporaryDirectory directory("Tracing.AnalyticEventsCapturedDuringWorkload");
    auto summary = CaptureWorkloadTrace(directory);

    constexpr uint64_t MinimumEvents = 100;
    constexpr size_t MinimumDistinctTypes = 3;
    if (summary.totalEvents < MinimumEvents
        || summary.distinctTypes.size() < MinimumDistinctTypes)
    {
        throw ScenarioFailure(std::format(
            "expected a substantial ese:* capture but got {} events across {} "
            "types — verify libese_tracepoints.so is staged next to libese.so "
            "and lttng-ust is functional",
            summary.totalEvents, summary.distinctTypes.size()));
    }
}

// The transaction subsystem must emit a begin, a commit, and a rollback.
// The workload commits eight transactions and explicitly rolls one back,
// so all three lifecycle tracepoints must appear in the capture.
EseIntegrationScenario(Tracing, TransactionLifecycleEventsAppearInTrace, Regression)
{
    TemporaryDirectory directory("Tracing.TransactionLifecycleEventsAppearInTrace");
    auto summary = CaptureWorkloadTrace(directory);

    RequireEventTypePresent(summary, "TransactionBegin");
    RequireEventTypePresent(summary, "TransactionCommit");
    RequireEventTypePresent(summary, "TransactionRollback");
}

// Analytic tracing must span the engine's subsystems, not just one. A
// page-allocating, log-flushing, IO-driving workload must surface buffer
// manager, IO, space management, and logging tracepoints together.
EseIntegrationScenario(Tracing, AnalyticEventsSpanCacheIoSpaceAndLog, Regression)
{
    TemporaryDirectory directory("Tracing.AnalyticEventsSpanCacheIoSpaceAndLog");
    auto summary = CaptureWorkloadTrace(directory);

    RequireEventTypePresent(summary, "CacheNewPage");
    RequireEventTypePresent(summary, "IOCompletion");
    RequireEventTypePresent(summary, "SpaceAllocPage");
    RequireEventTypePresent(summary, "LogWrite");
}

// Enabling only a subset of events must capture exactly that subset. This
// is the regression guard for the call-site gate's etguid -> enable-state
// indexing: PosixTraceEnabled reads eventEnabledState[etguid], so if that
// array is misaligned with the _etguid* enum, a selectively-enabled event
// would read the wrong tracepoint's flag and never come through (while
// some other event leaks). Two events at different enum positions are
// enabled; both must appear and the unenabled ones must not.
EseIntegrationScenario(Tracing, PerEventEnableGatesByEtguid, Regression)
{
    TemporaryDirectory directory("Tracing.PerEventEnableGatesByEtguid");
    auto summary =
        CaptureWorkloadTrace(directory, "ese:TransactionCommit,ese:CacheNewPage");

    RequireEventTypePresent(summary, "TransactionCommit");
    RequireEventTypePresent(summary, "CacheNewPage");

    RequireEventTypeAbsent(summary, "TransactionBegin");
    RequireEventTypeAbsent(summary, "LogWrite");
}
