// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// ese-tests entry point. Two modes:
//
// 1. Normal scenario runner. Walks ScenarioRegistry, filters by
// --filter glob, runs the survivors, prints pass/fail summary.
//
// 2. --child-entry <name> --child-directory <path>. Used by
// ChildProcess fork+exec; looks up the named entry in
// CrashHelper's registry and invokes it. The child is expected
// to be SIGKILL'd by the parent at some point, so we don't aim
// for clean shutdown.

#include "Framework/Check.hxx"
#include "Framework/Console.hxx"
#include "Framework/CrashHelper.hxx"
#include "Framework/ScaleProfile.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/ScenarioRegistry.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <jetapi.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace ese::tests;

namespace
{

struct CommandLineOptions
{
    std::string FilterPattern;
    ScaleProfileSize ScaleSize = ScaleProfileSize::Small;
    bool ListOnly = false;
    bool Verbose = false;
    bool KeepTemporary = false;
    // LongRunning scenarios are opt-in: they take minutes apiece and
    // exist to hammer the engine, not to gate fast iteration. Off by
    // default; --include-long-running flips them on.
    //  Tier filter.  Empty means "default selection" (smoke + reg).
    //  Populated via --tier <name>[,<name>...] with aliases:
    //    smoke
    //    reg | regression
    //    long | long-running | longrunning
    //    all
    std::vector<ScenarioTier> SelectedTiers;
    //  Each scenario runs in its own forked child by default so the
    //  parent never accumulates ESE process-global state (multi-instance
    //  flip, CResourceManager freeze, leftover handles).  --in-process
    //  collapses that for debugger attachment / asan walks where fork
    //  is inconvenient — but loses the isolation guarantee.
    bool InProcess = false;
    bool ChildMode = false;
    std::string ChildEntryName;
    std::string ChildDirectory;
    //  Any tokens that appear in argv after --child-directory in
    //  child-entry mode.  These reach the child entry function as
    //  `extraArgs` and carry per-child state (TCP ports, role tags,
    //  peer endpoints).
    std::vector<std::string> ChildExtraArgs;
    //  --log-file <path>.  When set, every line PrintLine emits is
    //  duplicated to <path> in addition to stdout.  Removes the need
    //  for `| tee` at the invocation site.
    std::string LogFile;
};


void PrintUsage(std::string_view programName)
{
    PrintLine("Usage: {} [options]", programName);
    PrintLine("");
    PrintLine("Options:");
    PrintLine(" --filter <pattern> Run only scenarios whose FullName matches <pattern>.");
    PrintLine(" Glob: '*' and '?' supported. Default: '*'");
    PrintLine(" --scale Small|Medium|Large Workload size profile. Default: Small.");
    PrintLine(" --list Print every registered scenario and exit.");
    PrintLine(" --verbose Print per-scenario start lines and failure detail.");
    PrintLine(" --keep-temp Don't delete scenario temp directories on exit.");
    PrintLine(" --in-process Run every scenario in the runner process (no fork).");
    PrintLine("              Off by default — fork-per-scenario isolates ESE's");
    PrintLine("              process-global state.  Use only for debugger attach.");
    PrintLine(" --tier <list> Comma-separated tier filter.  Tokens: smoke,");
    PrintLine("               reg|regression, long|long-running, all.  Default");
    PrintLine("               when omitted: smoke,reg (LongRunning excluded).");
    PrintLine(" --help, -h Show this message.");
    PrintLine("");
    PrintLine(" --log-file <path> Also write every console line to <path>.");
    PrintLine("");
    PrintLine("Child-entry mode (used internally for crash-recovery scenarios):");
    PrintLine(" --child-entry <name> Run the named child-entry callback and exit.");
    PrintLine(" --child-directory <path> Scratch directory the child should use.");
}

bool ParseCommandLine(int argc, char** argv, CommandLineOptions& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        auto requireValue = [&](std::string_view name) -> const char*
        {
            if (index + 1 >= argc)
            {
                PrintLine("missing value for {}", name);
                return nullptr;
            }
            return argv[++index];
        };

        if (argument == "--filter")
        {
            auto* value = requireValue(argument);
            if (value == nullptr)
            {
                return false;
            }
            options.FilterPattern = value;
        }
        else if (argument == "--scale")
        {
            auto* value = requireValue(argument);
            if (value == nullptr)
            {
                return false;
            }
            const std::string_view valueView = value;
            if (valueView == "Small")
            {
                options.ScaleSize = ScaleProfileSize::Small;
            }
            else if (valueView == "Medium")
            {
                options.ScaleSize = ScaleProfileSize::Medium;
            }
            else if (valueView == "Large")
            {
                options.ScaleSize = ScaleProfileSize::Large;
            }
            else
            {
                PrintLine("unknown --scale value: {}", valueView);
                return false;
            }
        }
        else if (argument == "--list")
        {
            options.ListOnly = true;
        }
        else if (argument == "--verbose")
        {
            options.Verbose = true;
        }
        else if (argument == "--keep-temp")
        {
            options.KeepTemporary = true;
        }
        else if (argument == "--in-process")
        {
            options.InProcess = true;
        }
        else if (argument == "--tier")
        {
            auto* value = requireValue(argument);
            if (value == nullptr)
            {
                return false;
            }
            //  Comma-separated tokens.  Validate every token; reject
            //  the whole list on the first unrecognized one.
            std::string_view list(value);
            size_t cursor = 0;
            while (cursor <= list.size())
            {
                const size_t comma = list.find(',', cursor);
                const auto token = list.substr(
                    cursor,
                    comma == std::string_view::npos
                        ? std::string_view::npos
                        : comma - cursor);
                if (!token.empty())
                {
                    ScenarioTier parsed = ScenarioTier::Smoke;
                    bool isAll = false;
                    if (!ParseScenarioTier(token, parsed, isAll))
                    {
                        PrintLine("unknown --tier token: {}", token);
                        return false;
                    }
                    if (isAll)
                    {
                        options.SelectedTiers = {
                            ScenarioTier::Smoke,
                            ScenarioTier::Regression,
                            ScenarioTier::LongRunning,
                        };
                    }
                    else
                    {
                        options.SelectedTiers.push_back(parsed);
                    }
                }
                if (comma == std::string_view::npos)
                {
                    break;
                }
                cursor = comma + 1;
            }
        }
        else if (argument == "--log-file")
        {
            auto* value = requireValue(argument);
            if (value == nullptr)
            {
                return false;
            }
            options.LogFile = value;
        }
        else if (argument == "--child-entry")
        {
            auto* value = requireValue(argument);
            if (value == nullptr)
            {
                return false;
            }
            options.ChildMode = true;
            options.ChildEntryName = value;
        }
        else if (argument == "--child-directory")
        {
            auto* value = requireValue(argument);
            if (value == nullptr)
            {
                return false;
            }
            options.ChildDirectory = value;
            //  Anything after --child-directory is fed verbatim to the
            //  child entry as extraArgs.  This is how the parent
            //  hands per-child state (TCP ports, role flags) across
            //  the fork+execve boundary — no on-disk config files.
            while (index + 1 < argc)
            {
                ++index;
                options.ChildExtraArgs.emplace_back(argv[index]);
            }
        }
        else if (argument == "--help" || argument == "-h")
        {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        else
        {
            PrintLine("unknown argument: {}", argument);
            return false;
        }
    }
    return true;
}

int RunChildEntry(const CommandLineOptions& options)
{
    if (options.ChildDirectory.empty())
    {
        PrintLine("--child-entry requires --child-directory");
        return 2;
    }
    auto* entry = FindChildEntry(options.ChildEntryName);
    if (entry == nullptr)
    {
        PrintLine("unknown child-entry: {}", options.ChildEntryName);
        return 2;
    }
    try
    {
        std::vector<std::string_view> argViews;
        argViews.reserve(options.ChildExtraArgs.size());
        for (const auto& token : options.ChildExtraArgs)
        {
            argViews.emplace_back(token);
        }
        (*entry)(std::filesystem::path(options.ChildDirectory),
                 std::span<const std::string_view>(argViews));
    }
    catch (const std::exception& exception)
    {
        PrintLine("child-entry {} threw: {}",
                  options.ChildEntryName, exception.what());
        return 1;
    }
    return 0;
}

// RAII wrapper around JetPlatformInitialize / JetPlatformTerminate.
// Linux libese.so requires this one-shot platform init before any other
// Jet API; Windows folds the same plumbing into DllMain.
//
// Defined ahead of the scenario loop because RunScenarios constructs
// one in each forked child — that way the engine's worker threads
// live in the child process (fork() inherits memory but not threads,
// so a parent-side initializer would leave the engine half-wired in
// the children that actually call Jet APIs).
class PlatformInitializer
{
public:
    PlatformInitializer()
    {
        auto initializeErrorCode = JetPlatformInitialize();
        if (initializeErrorCode < JET_errSuccess)
        {
            PrintLine("JetPlatformInitialize failed: {} ({})",
                      JetErrorName(initializeErrorCode),
                      static_cast<int>(initializeErrorCode));
            std::exit(2);
        }

        // Process-global engine settings — must be in place before any
        // scenario triggers internal OS-layer init. AssertAction in
        // particular must be SkipAll, otherwise FireWall paths in the
        // log layer abort the runner on ZFS-class filesystems that
        // report > 4 KB sector size. DisablePerfmon matches the
        // perfmon-off configuration libese.so is built with.
        auto assertActionErrorCode = JetSetSystemParameterA(nullptr,
                                                            JET_sesidNil,
                                                            JET_paramAssertAction,
                                                            JET_AssertSkipAll,
                                                            nullptr);
        if (assertActionErrorCode < JET_errSuccess)
        {
            PrintLine("JetSetSystemParameter(AssertAction) failed: {}",
                      JetErrorName(assertActionErrorCode));
            std::exit(2);
        }
        auto disablePerfmonErrorCode = JetSetSystemParameterA(nullptr,
                                                              JET_sesidNil,
                                                              JET_paramDisablePerfmon,
                                                              1,
                                                              nullptr);
        if (disablePerfmonErrorCode < JET_errSuccess)
        {
            PrintLine("JetSetSystemParameter(DisablePerfmon) failed: {}",
                      JetErrorName(disablePerfmonErrorCode));
            std::exit(2);
        }
    }
    ~PlatformInitializer()
    {
        (void)JetPlatformTerminate();
    }

    PlatformInitializer(const PlatformInitializer&) = delete;
    PlatformInitializer& operator=(const PlatformInitializer&) = delete;
};

struct ScenarioResult
{
    bool Passed = false;
    std::string FailureMessage;
};

ScenarioResult RunScenarioInProcess(Scenario* scenario)
{
    ScenarioResult result;
    try
    {
        scenario->Run();
        result.Passed = true;
    }
    catch (const ScenarioFailure& failure)
    {
        result.FailureMessage = failure.what();
    }
    catch (const std::exception& exception)
    {
        result.FailureMessage = std::format("std::exception: {}",
                                            exception.what());
    }
    catch (...)
    {
        result.FailureMessage = "unknown exception";
    }
    return result;
}

//  Read everything from a pipe fd until EOF.  Small failure messages
//  fit well below the default 64 KiB pipe buffer, so a blocking read
//  loop is sufficient — no need for poll/select.
std::string ReadAllFromPipe(int readFd)
{
    std::string out;
    char buffer[4096];
    while (true)
    {
        ssize_t n = ::read(readFd, buffer, sizeof(buffer));
        if (n > 0)
        {
            out.append(buffer, static_cast<size_t>(n));
        }
        else if (n == 0)
        {
            break;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            break;
        }
    }
    return out;
}

ScenarioResult RunScenarioInForkedChild(Scenario* scenario)
{
    //  Single side-channel pipe carries the failure message from the
    //  child back to the parent.  Child stdout/stderr stay attached to
    //  the parent's terminal so PrintLine diagnostics from inside the
    //  scenario behave exactly as they did before isolation.
    int failurePipe[2] = { -1, -1 };
    if (::pipe(failurePipe) != 0)
    {
        return { false,
                 std::format("pipe() failed: {}", std::strerror(errno)) };
    }

    pid_t childPid = ::fork();
    if (childPid < 0)
    {
        ::close(failurePipe[0]);
        ::close(failurePipe[1]);
        return { false,
                 std::format("fork() failed: {}", std::strerror(errno)) };
    }

    if (childPid == 0)
    {
        //  Child: close read end, run the scenario with a fresh engine
        //  initializer, write failure (if any) into the pipe, _exit so
        //  parent-side C++ destructors don't double-fire on shared
        //  resources like g_logFile.
        ::close(failurePipe[0]);

        //  PlatformInitializer is RAII, but since we _exit at the end
        //  the destructor (JetPlatformTerminate) never runs — that's
        //  fine: the process is going away and the kernel reclaims
        //  everything ESE owns.
        PlatformInitializer platformInitializer;
        (void)platformInitializer;

        ScenarioResult childResult = RunScenarioInProcess(scenario);

        if (!childResult.Passed && !childResult.FailureMessage.empty())
        {
            const auto& message = childResult.FailureMessage;
            size_t written = 0;
            while (written < message.size())
            {
                ssize_t n = ::write(failurePipe[1],
                                    message.data() + written,
                                    message.size() - written);
                if (n > 0)
                {
                    written += static_cast<size_t>(n);
                }
                else if (n < 0 && errno == EINTR)
                {
                    continue;
                }
                else
                {
                    break;
                }
            }
        }
        ::close(failurePipe[1]);
        std::_Exit(childResult.Passed ? 0 : 1);
    }

    //  Parent: close write end, drain the pipe, wait, decode status.
    ::close(failurePipe[1]);
    std::string failureFromChild = ReadAllFromPipe(failurePipe[0]);
    ::close(failurePipe[0]);

    int status = 0;
    while (::waitpid(childPid, &status, 0) < 0)
    {
        if (errno == EINTR)
        {
            continue;
        }
        return { false,
                 std::format("waitpid() failed: {}", std::strerror(errno)) };
    }

    if (WIFEXITED(status))
    {
        const int code = WEXITSTATUS(status);
        if (code == 0)
        {
            return { true, "" };
        }
        if (!failureFromChild.empty())
        {
            return { false, std::move(failureFromChild) };
        }
        return { false, std::format("child exited with code {}", code) };
    }
    if (WIFSIGNALED(status))
    {
        const int sig = WTERMSIG(status);
        return { false,
                 std::format("child terminated by signal {} ({}){}",
                             sig,
                             ::strsignal(sig),
                             failureFromChild.empty()
                                 ? std::string()
                                 : std::format(": {}", failureFromChild)) };
    }
    return { false, "child exited abnormally" };
}

int RunScenarios(const CommandLineOptions& options)
{
    TemporaryDirectory::SetKeepOnDestruction(options.KeepTemporary);
    SetActiveScaleProfile(options.ScaleSize);

    auto& registry = ScenarioRegistry::Instance();
    const auto pattern = options.FilterPattern.empty() ? "*" : options.FilterPattern;
    const auto matchedScenarios = registry.Filtered(pattern);

    //  Tier selection.  Default (no --tier flag): smoke + reg.
    //  Two escape hatches that auto-include LongRunning even
    //  without explicit selection:
    //    (a) filter pattern matches LongRunning-tier scenarios
    //        exclusively (operator named them — trust the intent),
    //    (b) the user passed --tier with an explicit set.
    std::vector<ScenarioTier> selectedTiers = options.SelectedTiers;
    bool tierSelectionExplicit = !selectedTiers.empty();
    if (selectedTiers.empty())
    {
        selectedTiers = {
            ScenarioTier::Smoke,
            ScenarioTier::Regression,
        };
    }

    if (!tierSelectionExplicit)
    {
        bool everyMatchIsLongRunning = !matchedScenarios.empty();
        for (auto* scenario : matchedScenarios)
        {
            if (scenario->Tier() != ScenarioTier::LongRunning)
            {
                everyMatchIsLongRunning = false;
                break;
            }
        }
        if (everyMatchIsLongRunning)
        {
            selectedTiers.push_back(ScenarioTier::LongRunning);
        }
    }

    auto tierAllowed = [&](ScenarioTier candidate)
    {
        for (auto tier : selectedTiers)
        {
            if (tier == candidate)
            {
                return true;
            }
        }
        return false;
    };

    std::vector<Scenario*> matching;
    matching.reserve(matchedScenarios.size());
    uint32_t suppressedByTier = 0;
    for (auto* scenario : matchedScenarios)
    {
        if (!tierAllowed(scenario->Tier()))
        {
            ++suppressedByTier;
            continue;
        }
        matching.push_back(scenario);
    }

    if (options.ListOnly)
    {
        for (auto* scenario : matching)
        {
            PrintLine("[{:<5}] {}",
                      ToString(scenario->Tier()),
                      scenario->FullName());
        }
        if (suppressedByTier > 0)
        {
            PrintLine("(skipping {} scenario(s) outside the selected tier(s); "
                      "pass --tier all or name the tier explicitly to include)",
                      suppressedByTier);
        }
        return 0;
    }

    if (matching.empty())
    {
        PrintLine("no scenarios match filter '{}'", pattern);
        return 1;
    }

    int passedCount = 0;
    int failedCount = 0;

    const auto suiteStart = std::chrono::steady_clock::now();

    for (auto* scenario : matching)
    {
        const auto fullName = scenario->FullName();
        if (options.Verbose)
        {
            PrintLine("[ RUN ] {}", fullName);
        }

        const auto scenarioStart = std::chrono::steady_clock::now();
        ScenarioResult result = options.InProcess
            ? RunScenarioInProcess(scenario)
            : RunScenarioInForkedChild(scenario);

        const auto scenarioDuration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - scenarioStart);

        if (result.Passed)
        {
            ++passedCount;
            PrintLine("[ PASS ] {} ({} ms)",
                      fullName, scenarioDuration.count());
        }
        else
        {
            ++failedCount;
            PrintLine("[ FAIL ] {} ({} ms)",
                      fullName, scenarioDuration.count());
            PrintLine(" {}", result.FailureMessage);
        }
    }

    const auto suiteDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - suiteStart);

    PrintLine("");
    PrintLine("=== {} passed, {} failed ({} ms total) ===",
              passedCount, failedCount, suiteDuration.count());

    return failedCount == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    CommandLineOptions options;
    if (!ParseCommandLine(argc, argv, options))
    {
        PrintUsage(argv[0]);
        return 2;
    }

    // Mirror every PrintLine to --log-file if one was supplied. Open
    // it before any other work so engine startup errors land in the
    // log too. Child processes inherit the parent's actual fd 1; the
    // mirror is process-local and doesn't follow them.
    if (!options.LogFile.empty())
    {
        if (!OpenLogFile(options.LogFile))
        {
            PrintLine("failed to open --log-file: {}", options.LogFile);
            return 2;
        }
    }

    int exitCode;
    if (options.ChildMode)
    {
        //  --child-entry is the leaf of a fork+exec from inside a
        //  scenario.  This process IS the engine — initialize it.
        PlatformInitializer platformInitializer;
        (void)platformInitializer;
        exitCode = RunChildEntry(options);
    }
    else if (options.InProcess)
    {
        //  --in-process: every scenario runs in this process.  Init
        //  the engine once up front, same as the pre-isolation runner.
        PlatformInitializer platformInitializer;
        (void)platformInitializer;
        exitCode = RunScenarios(options);
    }
    else
    {
        //  Default fork-per-scenario.  The parent must NOT initialize
        //  the engine — engine worker threads do not survive fork(),
        //  so each forked child runs PlatformInitializer for itself
        //  inside RunScenarioInForkedChild.
        exitCode = RunScenarios(options);
    }
    CloseLogFile();
    return exitCode;
}
