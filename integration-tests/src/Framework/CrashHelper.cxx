// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/CrashHelper.hxx"

#include "Framework/Check.hxx"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ese::tests
{

namespace
{

constexpr std::string_view ReadySentinelFileName = "child-ready.sentinel";

std::unordered_map<std::string, ChildEntryPoint>& ChildEntryTable()
{
    static std::unordered_map<std::string, ChildEntryPoint> table;
    return table;
}

// Path to /proc/self/exe — the binary we're going to fork+exec.
std::string ResolveSelfExecutablePath()
{
    char executablePathBuffer[4096] = { 0 };
    const auto bytesRead = readlink("/proc/self/exe",
                                    executablePathBuffer,
                                    sizeof(executablePathBuffer) - 1);
    if (bytesRead <= 0)
    {
        throw std::runtime_error("readlink(/proc/self/exe) failed");
    }
    executablePathBuffer[bytesRead] = '\0';
    return std::string(executablePathBuffer);
}

} // namespace

void RegisterChildEntry(std::string name, ChildEntryPoint entry)
{
    ChildEntryTable().emplace(std::move(name), std::move(entry));
}

void RegisterChildEntry(std::string name, SimpleChildEntryPoint entry)
{
    ChildEntryTable().emplace(
        std::move(name),
        [entry = std::move(entry)](const std::filesystem::path& directory,
                                   std::span<const std::string_view>)
        {
            entry(directory);
        });
}

const ChildEntryPoint* FindChildEntry(std::string_view name)
{
    auto& table = ChildEntryTable();
    auto iterator = table.find(std::string(name));
    if (iterator == table.end())
    {
        return nullptr;
    }
    return &iterator->second;
}

void ChildProcess::SignalReady(const std::filesystem::path& directory)
{
    std::ofstream sentinelFile(directory / ReadySentinelFileName, std::ios::trunc);
    sentinelFile << "ready" << std::endl;
}

ChildProcess::ChildProcess(std::string_view entryName,
                           const std::filesystem::path& directory,
                           std::span<const std::string> extraArgs)
    : _directory(directory)
{
    const auto entryNameOwned = std::string(entryName);
    const auto directoryString = directory.string();
    const auto executablePath = ResolveSelfExecutablePath();

    // Build argv for execve. All arguments must outlive the call.
    std::vector<std::string> argumentStorage;
    argumentStorage.reserve(5 + extraArgs.size());
    argumentStorage.push_back(executablePath);
    argumentStorage.push_back("--child-entry");
    argumentStorage.push_back(entryNameOwned);
    argumentStorage.push_back("--child-directory");
    argumentStorage.push_back(directoryString);
    for (const auto& extraArg : extraArgs)
    {
        argumentStorage.push_back(extraArg);
    }

    std::vector<char*> argumentPointers;
    argumentPointers.reserve(argumentStorage.size() + 1);
    for (auto& argument : argumentStorage)
    {
        argumentPointers.push_back(argument.data());
    }
    argumentPointers.push_back(nullptr);

    const auto childPid = fork();
    if (childPid < 0)
    {
        throw std::runtime_error(std::format("fork() failed: {}", std::strerror(errno)));
    }

    if (childPid == 0)
    {
        // In the child. Replace ourselves with a fresh ese-tests
        // invocation in child-entry mode. No return on success.
        execve(executablePath.c_str(), argumentPointers.data(), environ);
        // If execve returns the child is dead anyway — print and exit.
        std::string failureMessage = std::format("execve({}) failed: {}\n",
                                                 executablePath,
                                                 std::strerror(errno));
        (void)write(STDERR_FILENO, failureMessage.data(), failureMessage.size());
        _exit(127);
    }

    _pid = childPid;
}

ChildProcess::~ChildProcess()
{
    if (_pid > 0 && !_reaped)
    {
        // Last-resort cleanup so we never leave a child zombie.
        ::kill(_pid, SIGKILL);
        int status = 0;
        (void)waitpid(_pid, &status, 0);
    }
}

void ChildProcess::WaitUntilReady(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto sentinelPath = _directory / ReadySentinelFileName;
    const auto pollInterval = std::chrono::milliseconds(10);

    while (std::chrono::steady_clock::now() < deadline)
    {
        std::error_code errorCode;
        if (std::filesystem::exists(sentinelPath, errorCode))
        {
            return;
        }
        // Detect early child death so we don't spin until the timeout.
        int status = 0;
        const auto result = waitpid(_pid, &status, WNOHANG);
        if (result == _pid)
        {
            _reaped = true;
            throw std::runtime_error(std::format(
                "child {} exited before signalling ready (status={})",
                static_cast<long>(_pid),
                status));
        }
        std::this_thread::sleep_for(pollInterval);
    }
    throw std::runtime_error(std::format("child {} did not become ready within {} ms",
                                         static_cast<long>(_pid),
                                         timeout.count()));
}

void ChildProcess::Kill()
{
    if (_pid > 0 && !_reaped)
    {
        ::kill(_pid, SIGKILL);
    }
}

int ChildProcess::WaitForExit()
{
    if (_reaped)
    {
        return 0;
    }
    int status = 0;
    if (waitpid(_pid, &status, 0) != _pid)
    {
        throw std::runtime_error(std::format("waitpid({}) failed: {}",
                                             static_cast<long>(_pid),
                                             std::strerror(errno)));
    }
    _reaped = true;
    return status;
}

} // namespace ese::tests
