// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/TemporaryDirectory.hxx"

#include <atomic>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <unistd.h>

namespace ese::tests
{

namespace
{

constexpr std::string_view TestsRoot = "/p/ese/temp/ese-tests";

bool _keepOnDestruction = false;

std::atomic<unsigned> _scenarioSequence{ 0 };

//  Replace anything that isn't a-z/A-Z/0-9/./_/- so the scenario name
//  is safe to embed in a filesystem path.
std::string SanitiseName(std::string_view name)
{
    std::string sanitised;
    sanitised.reserve(name.size());
    for (auto character : name)
    {
        const bool acceptable =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-';
        sanitised.push_back(acceptable ? character : '_');
    }
    return sanitised;
}

}  //  namespace

TemporaryDirectory::TemporaryDirectory(std::string_view scenarioName)
{
    const auto sequence  = _scenarioSequence.fetch_add(1, std::memory_order_relaxed);
    const auto processId = static_cast<long>(::getpid());

    _path = std::filesystem::path(TestsRoot)
            / std::format("{}-{}-{:04}", SanitiseName(scenarioName), processId, sequence);

    std::error_code errorCode;
    std::filesystem::create_directories(_path, errorCode);
    if (errorCode)
    {
        throw std::runtime_error(std::format("create_directories({}) failed: {}",
                                             _path.string(),
                                             errorCode.message()));
    }
}

TemporaryDirectory::~TemporaryDirectory()
{
    if (_keepOnDestruction)
    {
        return;
    }

    std::error_code errorCode;
    std::filesystem::remove_all(_path, errorCode);
    //  Best-effort cleanup; we deliberately swallow errors in the dtor
    //  so a failed cleanup doesn't mask the scenario's real result.
}

void TemporaryDirectory::SetKeepOnDestruction(bool keep)
{
    _keepOnDestruction = keep;
}

}  //  namespace ese::tests
