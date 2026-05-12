// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
//  ese-tests entry point.  Two modes:
//
//   1. Normal scenario runner.  Walks ScenarioRegistry, filters by
//      --filter glob, runs the survivors, prints pass/fail summary.
//
//   2. --child-entry <name> --child-directory <path>.  Used by
//      ChildProcess fork+exec; looks up the named entry in
//      CrashHelper's registry and invokes it.  The child is expected
//      to be SIGKILL'd by the parent at some point, so we don't aim
//      for clean shutdown.

#include "Framework/Check.hxx"
#include "Framework/CrashHelper.hxx"
#include "Framework/ScaleProfile.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/ScenarioRegistry.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <jet.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace
{

struct CommandLineOptions
{
    std::string                     FilterPattern;
    ese::tests::ScaleProfileSize    ScaleSize       = ese::tests::ScaleProfileSize::Small;
    bool                            ListOnly        = false;
    bool                            Verbose         = false;
    bool                            KeepTemporary   = false;
    bool                            ChildMode       = false;
    std::string                     ChildEntryName;
    std::string                     ChildDirectory;
};

void PrintUsage(std::string_view programName)
{
    std::cout << "Usage: " << programName << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --filter <pattern>         Run only scenarios whose FullName matches <pattern>.\n"
              << "                             Glob: '*' and '?' supported.  Default: '*'\n"
              << "  --scale Small|Medium|Large Workload size profile.  Default: Small.\n"
              << "  --list                     Print every registered scenario and exit.\n"
              << "  --verbose                  Print per-scenario start lines and failure detail.\n"
              << "  --keep-temp                Don't delete scenario temp directories on exit.\n"
              << "  --help, -h                 Show this message.\n"
              << "\n"
              << "Child-entry mode (used internally for crash-recovery scenarios):\n"
              << "  --child-entry <name>       Run the named child-entry callback and exit.\n"
              << "  --child-directory <path>   Scratch directory the child should use.\n";
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
                std::cerr << "missing value for " << name << "\n";
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
                options.ScaleSize = ese::tests::ScaleProfileSize::Small;
            }
            else if (valueView == "Medium")
            {
                options.ScaleSize = ese::tests::ScaleProfileSize::Medium;
            }
            else if (valueView == "Large")
            {
                options.ScaleSize = ese::tests::ScaleProfileSize::Large;
            }
            else
            {
                std::cerr << "unknown --scale value: " << valueView << "\n";
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
        else if (argument == "--child-entry")
        {
            auto* value = requireValue(argument);
            if (value == nullptr)
            {
                return false;
            }
            options.ChildMode      = true;
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
        }
        else if (argument == "--help" || argument == "-h")
        {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        else
        {
            std::cerr << "unknown argument: " << argument << "\n";
            return false;
        }
    }
    return true;
}

int RunChildEntry(const CommandLineOptions& options)
{
    if (options.ChildDirectory.empty())
    {
        std::cerr << "--child-entry requires --child-directory\n";
        return 2;
    }
    auto* entry = ese::tests::FindChildEntry(options.ChildEntryName);
    if (entry == nullptr)
    {
        std::cerr << "unknown child-entry: " << options.ChildEntryName << "\n";
        return 2;
    }
    try
    {
        (*entry)(std::filesystem::path(options.ChildDirectory));
    }
    catch (const std::exception& exception)
    {
        std::cerr << "child-entry " << options.ChildEntryName
                  << " threw: " << exception.what() << "\n";
        return 1;
    }
    return 0;
}

int RunScenarios(const CommandLineOptions& options)
{
    ese::tests::TemporaryDirectory::SetKeepOnDestruction(options.KeepTemporary);
    ese::tests::SetActiveScaleProfile(options.ScaleSize);

    auto& registry = ese::tests::ScenarioRegistry::Instance();
    const auto pattern = options.FilterPattern.empty() ? "*" : options.FilterPattern;
    const auto matching = registry.Filtered(pattern);

    if (options.ListOnly)
    {
        for (auto* scenario : matching)
        {
            std::cout << scenario->FullName() << "\n";
        }
        return 0;
    }

    if (matching.empty())
    {
        std::cerr << "no scenarios match filter '" << pattern << "'\n";
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
            std::cout << "[ RUN  ] " << fullName << "\n";
        }

        const auto scenarioStart = std::chrono::steady_clock::now();
        bool       passed        = false;
        std::string failureMessage;

        try
        {
            scenario->Run();
            passed = true;
        }
        catch (const ese::tests::ScenarioFailure& failure)
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
            std::cout << std::format("[ PASS ] {} ({} ms)\n",
                                     fullName,
                                     scenarioDuration.count());
        }
        else
        {
            ++failedCount;
            std::cout << std::format("[ FAIL ] {} ({} ms)\n",
                                     fullName,
                                     scenarioDuration.count())
                      << "         " << failureMessage << "\n";
        }
    }

    const auto suiteDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - suiteStart);

    std::cout << "\n"
              << std::format("=== {} passed, {} failed ({} ms total) ===\n",
                             passedCount,
                             failedCount,
                             suiteDuration.count());

    return failedCount == 0 ? 0 : 1;
}

//  RAII wrapper around JetPlatformInitialize / JetPlatformTerminate.
//  Linux libese.so requires this one-shot platform init before any other
//  Jet API; Windows folds the same plumbing into DllMain.
class PlatformInitializer
{
public:
    PlatformInitializer()
    {
        auto initializeErrorCode = JetPlatformInitialize();
        if (initializeErrorCode < JET_errSuccess)
        {
            std::cerr << "JetPlatformInitialize failed: "
                      << ese::tests::JetErrorName(initializeErrorCode)
                      << " (" << static_cast<int>(initializeErrorCode) << ")\n";
            std::exit(2);
        }

        //  Process-global engine settings — must be in place before any
        //  scenario triggers internal OS-layer init.  AssertAction in
        //  particular must be SkipAll, otherwise FireWall paths in the
        //  log layer abort the runner on ZFS-class filesystems that
        //  report > 4 KB sector size.  DisablePerfmon matches the
        //  perfmon-off configuration libese.so is built with.
        auto assertActionErrorCode = JetSetSystemParameterA(nullptr,
                                                            JET_sesidNil,
                                                            JET_paramAssertAction,
                                                            JET_AssertSkipAll,
                                                            nullptr);
        if (assertActionErrorCode < JET_errSuccess)
        {
            std::cerr << "JetSetSystemParameter(AssertAction) failed: "
                      << ese::tests::JetErrorName(assertActionErrorCode) << "\n";
            std::exit(2);
        }
        auto disablePerfmonErrorCode = JetSetSystemParameterA(nullptr,
                                                              JET_sesidNil,
                                                              JET_paramDisablePerfmon,
                                                              1,
                                                              nullptr);
        if (disablePerfmonErrorCode < JET_errSuccess)
        {
            std::cerr << "JetSetSystemParameter(DisablePerfmon) failed: "
                      << ese::tests::JetErrorName(disablePerfmonErrorCode) << "\n";
            std::exit(2);
        }
    }
    ~PlatformInitializer()
    {
        (void)JetPlatformTerminate();
    }

    PlatformInitializer(const PlatformInitializer&)            = delete;
    PlatformInitializer& operator=(const PlatformInitializer&) = delete;
};

}  //  namespace

int main(int argc, char** argv)
{
    CommandLineOptions options;
    if (!ParseCommandLine(argc, argv, options))
    {
        PrintUsage(argv[0]);
        return 2;
    }

    PlatformInitializer platformInitializer;

    if (options.ChildMode)
    {
        return RunChildEntry(options);
    }
    return RunScenarios(options);
}
