// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Console — single output funnel for the runner and every scenario.
// Writes go to stdout (only stdout; we don't use stderr) via
// std::println, and are mirrored to an optional log file path set via
// OpenLogFile. Scenarios print progress through PrintLine so their
// output ends up in --log-file alongside the runner banner.

#pragma once

#include <cstdio>
#include <print>
#include <string_view>
#include <utility>

namespace ese::tests
{

inline std::FILE* g_logFile = nullptr;

// Opens the log file and stores the FILE* for PrintLine to mirror
// every line into. Returns false (without aborting) if the path
// can't be opened; the runner decides what to do with that.
inline bool OpenLogFile(std::string_view path)
{
    g_logFile = std::fopen(std::string(path).c_str(), "w");
    return g_logFile != nullptr;
}

inline void CloseLogFile()
{
    if (g_logFile != nullptr)
    {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

// Format a line to stdout and, if --log-file was supplied, mirror it
// to that file. std::println appends a single newline; format strings
// passed here should NOT end with '\n'.
template <class... Args>
void PrintLine(std::format_string<Args...> formatString, Args&&... args)
{
    std::println(stdout, formatString, std::forward<Args>(args)...);
    if (g_logFile != nullptr)
    {
        std::println(g_logFile, formatString,
                     std::forward<Args>(args)...);
        std::fflush(g_logFile);
    }
}

} // namespace ese::tests
