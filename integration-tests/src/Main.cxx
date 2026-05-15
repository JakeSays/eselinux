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
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <streambuf>
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
    bool ChildMode = false;
    std::string ChildEntryName;
    std::string ChildDirectory;
    //  Any tokens that appear in argv after --child-directory in
    //  child-entry mode.  These reach the child entry function as
    //  `extraArgs` and carry per-child state (TCP ports, role tags,
    //  peer endpoints).
    std::vector<std::string> ChildExtraArgs;
    //  --log-file <path>.  When set, every write to std::cout and
    //  std::cerr is duplicated to <path> in addition to the original
    //  stdout/stderr fds.  Removes the need for `| tee` at the
    //  invocation site.
    std::string LogFile;
};

//  A std::streambuf that forwards every write to two upstream
//  streambufs.  Used to plumb std::cout / std::cerr into both their
//  original destination AND the --log-file path simultaneously.
class TeeStreambuf : public std::streambuf
{
public:
    TeeStreambuf(std::streambuf* primary, std::streambuf* secondary)
        : _primary(primary), _secondary(secondary)
    {
    }

protected:
    int overflow(int character) override
    {
        if (character == traits_type::eof())
        {
            return traits_type::not_eof(character);
        }
        const auto a = _primary != nullptr
                       ? _primary->sputc(static_cast<char>(character))
                       : character;
        const auto b = _secondary != nullptr
                       ? _secondary->sputc(static_cast<char>(character))
                       : character;
        return (a == traits_type::eof() || b == traits_type::eof())
                ? traits_type::eof()
                : character;
    }

    std::streamsize xsputn(const char* data, std::streamsize count) override
    {
        const auto a = _primary != nullptr
                       ? _primary->sputn(data, count)
                       : count;
        const auto b = _secondary != nullptr
                       ? _secondary->sputn(data, count)
                       : count;
        return std::min(a, b);
    }

    int sync() override
    {
        const auto a = _primary != nullptr ? _primary->pubsync() : 0;
        const auto b = _secondary != nullptr ? _secondary->pubsync() : 0;
        return (a == 0 && b == 0) ? 0 : -1;
    }

private:
    std::streambuf* _primary;
    std::streambuf* _secondary;
};

//  RAII guard that installs TeeStreambuf as the active buffer for
//  std::cout and std::cerr, and restores the originals at scope exit.
class ConsoleLogTee
{
public:
    explicit ConsoleLogTee(const std::string& logPath)
        : _logFile(logPath, std::ios::out | std::ios::trunc)
    {
        if (!_logFile.is_open())
        {
            throw std::runtime_error("failed to open --log-file: " + logPath);
        }
        _coutTee = std::make_unique<TeeStreambuf>(std::cout.rdbuf(),
                                                  _logFile.rdbuf());
        _cerrTee = std::make_unique<TeeStreambuf>(std::cerr.rdbuf(),
                                                  _logFile.rdbuf());
        _originalCout = std::cout.rdbuf(_coutTee.get());
        _originalCerr = std::cerr.rdbuf(_cerrTee.get());
    }

    ~ConsoleLogTee()
    {
        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf(_originalCout);
        std::cerr.rdbuf(_originalCerr);
    }

    ConsoleLogTee(const ConsoleLogTee&) = delete;
    ConsoleLogTee& operator=(const ConsoleLogTee&) = delete;

private:
    std::ofstream _logFile;
    std::unique_ptr<TeeStreambuf> _coutTee;
    std::unique_ptr<TeeStreambuf> _cerrTee;
    std::streambuf* _originalCout = nullptr;
    std::streambuf* _originalCerr = nullptr;
};

void PrintUsage(std::string_view programName)
{
    std::cout << "Usage: " << programName << " [options]\n"
              << "\n"
              << "Options:\n"
              << " --filter <pattern> Run only scenarios whose FullName matches <pattern>.\n"
              << " Glob: '*' and '?' supported. Default: '*'\n"
              << " --scale Small|Medium|Large Workload size profile. Default: Small.\n"
              << " --list Print every registered scenario and exit.\n"
              << " --verbose Print per-scenario start lines and failure detail.\n"
              << " --keep-temp Don't delete scenario temp directories on exit.\n"
              << " --help, -h Show this message.\n"
              << "\n"
              << " --log-file <path> Also write every console line to <path>.\n"
              << "\n"
              << "Child-entry mode (used internally for crash-recovery scenarios):\n"
              << " --child-entry <name> Run the named child-entry callback and exit.\n"
              << " --child-directory <path> Scratch directory the child should use.\n";
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
    auto* entry = FindChildEntry(options.ChildEntryName);
    if (entry == nullptr)
    {
        std::cerr << "unknown child-entry: " << options.ChildEntryName << "\n";
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
        std::cerr << "child-entry " << options.ChildEntryName
                  << " threw: " << exception.what() << "\n";
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
            std::cout << "[ RUN ] " << fullName << "\n";
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
                      << " " << failureMessage << "\n";
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
            std::cerr << "JetPlatformInitialize failed: "
                      << JetErrorName(initializeErrorCode)
                      << " (" << static_cast<int>(initializeErrorCode) << ")\n";
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
            std::cerr << "JetSetSystemParameter(AssertAction) failed: "
                      << JetErrorName(assertActionErrorCode) << "\n";
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
                      << JetErrorName(disablePerfmonErrorCode) << "\n";
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

    //  Mirror all parent-process stdout/stderr writes to --log-file,
    //  if requested.  Construct the guard before any other work so we
    //  also capture engine startup errors, JetPlatformInitialize
    //  diagnostics, etc.  Child processes inherit the parent's actual
    //  fd 1/2 — the tee only affects this process's std::cout / cerr,
    //  not its file descriptors, so children still write to whatever
    //  the parent's stdout/stderr point at.
    std::unique_ptr<ConsoleLogTee> consoleLogTee;
    if (!options.LogFile.empty())
    {
        try
        {
            consoleLogTee = std::make_unique<ConsoleLogTee>(options.LogFile);
        }
        catch (const std::exception& exception)
        {
            std::cerr << exception.what() << "\n";
            return 2;
        }
    }

    PlatformInitializer platformInitializer;

    if (options.ChildMode)
    {
        return RunChildEntry(options);
    }
    return RunScenarios(options);
}
