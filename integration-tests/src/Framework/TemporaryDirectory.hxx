// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
//  TemporaryDirectory — owns a scratch path under /p/ese/temp/ese-tests/
//  for the lifetime of a scenario.  Removes the tree on destruction
//  (suppressed in --keep-temp mode).

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ese::tests
{

class TemporaryDirectory
{
public:
    explicit TemporaryDirectory(std::string_view scenarioName);
    ~TemporaryDirectory();

    TemporaryDirectory(const TemporaryDirectory&)            = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& Path() const
    {
        return _path;
    }

    //  When set, the directory survives destruction.  Driven by the
    //  --keep-temp command-line flag.
    static void SetKeepOnDestruction(bool keep);

private:
    std::filesystem::path _path;
};

}  //  namespace ese::tests
