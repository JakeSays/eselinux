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
    bool IncludeLongRunning = false;
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
    PrintLine(" --include-long-running Force-include the LongRunning category in a");
    PrintLine("                        broader run. Not needed when --filter matches");
    PrintLine("                        only LongRunning scenarios.");
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
        else if (argument == "--include-long-running")
        {
            options.IncludeLongRunning = true;
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

int RunScenarios(const CommandLineOptions& options)
{
    TemporaryDirectory::SetKeepOnDestruction(options.KeepTemporary);
    SetActiveScaleProfile(options.ScaleSize);

    auto& registry = ScenarioRegistry::Instance();
    const auto pattern = options.FilterPattern.empty() ? "*" : options.FilterPattern;
    const auto matchedScenarios = registry.Filtered(pattern);

    // LongRunning scenarios are suppressed by default — minutes apiece,
    // they would dominate any unfiltered run.  Two paths include them:
    // an explicit --include-long-running, or a filter pattern that
    // matches LongRunning scenarios exclusively (the operator named
    // them, so we trust the intent).
    bool everyMatchIsLongRunning = !matchedScenarios.empty();
    for (auto* scenario : matchedScenarios)
    {
        if (scenario->Category() != ScenarioCategory::LongRunning)
        {
            everyMatchIsLongRunning = false;
            break;
        }
    }
    const bool includeLongRunning =
        options.IncludeLongRunning || everyMatchIsLongRunning;

    std::vector<Scenario*> matching;
    matching.reserve(matchedScenarios.size());
    uint32_t suppressedLongRunningCount = 0;
    for (auto* scenario : matchedScenarios)
    {
        if (scenario->Category() == ScenarioCategory::LongRunning
            && !includeLongRunning)
        {
            ++suppressedLongRunningCount;
            continue;
        }
        matching.push_back(scenario);
    }

    if (options.ListOnly)
    {
        for (auto* scenario : matching)
        {
            PrintLine("{}", scenario->FullName());
        }
        if (suppressedLongRunningCount > 0)
        {
            PrintLine("(skipping {} LongRunning scenario(s); "
                      "pass --include-long-running or filter them "
                      "explicitly to enable)",
                      suppressedLongRunningCount);
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
        bool passed = false;
        std::string failureMessage;

        try
        {
            scenario->Run();
            passed = true;
        }
        catch (const ScenarioFailure& failure)
        {
            failureMessage = failure.what();
        }
        catch (const std::exception& exception)
        {
            failureMessage = std::format("std::exception: {}", exception.what());
        }
        catch (...)
        {
            failureMessage = "unknown exception";
        }

        const auto scenarioDuration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - scenarioStart);

        if (passed)
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
            PrintLine(" {}", failureMessage);
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

// RAII wrapper around JetPlatformInitialize / JetPlatformTerminate.
// Linux libese.so requires this one-shot platform init before any other
// Jet API; Windows folds the same plumbing into DllMain.
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

    PlatformInitializer platformInitializer;

    int exitCode;
    if (options.ChildMode)
    {
        exitCode = RunChildEntry(options);
    }
    else
    {
        exitCode = RunScenarios(options);
    }
    CloseLogFile();
    return exitCode;
}
