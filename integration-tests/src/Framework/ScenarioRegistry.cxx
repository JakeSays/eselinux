// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/ScenarioRegistry.hxx"

#include "Framework/Scenario.hxx"

#include <algorithm>
#include <string>
#include <string_view>

namespace ese::tests
{

namespace
{

//  Tiny glob matcher: `*` matches zero or more of any character,
//  `?` matches exactly one.  Sufficient for `--filter Schema.*` or
//  `--filter *Backup*`.
bool MatchesGlob(std::string_view pattern, std::string_view text)
{
    if (pattern.empty())
    {
        return true;
    }

    //  Iterative backtracking — small inputs, no recursion concern.
    size_t patternIndex      = 0;
    size_t textIndex         = 0;
    size_t starPatternIndex  = std::string_view::npos;
    size_t starTextIndex     = 0;

    while (textIndex < text.size())
    {
        if (patternIndex < pattern.size() &&
            (pattern[patternIndex] == '?' || pattern[patternIndex] == text[textIndex]))
        {
            ++patternIndex;
            ++textIndex;
        }
        else if (patternIndex < pattern.size() && pattern[patternIndex] == '*')
        {
            starPatternIndex = patternIndex;
            starTextIndex    = textIndex;
            ++patternIndex;
        }
        else if (starPatternIndex != std::string_view::npos)
        {
            patternIndex = starPatternIndex + 1;
            ++starTextIndex;
            textIndex = starTextIndex;
        }
        else
        {
            return false;
        }
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*')
    {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

}  //  namespace

ScenarioRegistry& ScenarioRegistry::Instance()
{
    //  Meyers singleton — guaranteed initialised on first use, which is
    //  before any Scenario ctor runs because the function-local static
    //  is constructed on demand from Register().
    static ScenarioRegistry instance;
    return instance;
}

void ScenarioRegistry::Register(Scenario* scenario)
{
    _scenarios.push_back(scenario);
}

std::vector<Scenario*> ScenarioRegistry::All() const
{
    auto copy = _scenarios;
    std::sort(copy.begin(), copy.end(), [](auto* leftScenario, auto* rightScenario)
    {
        if (leftScenario->Category() != rightScenario->Category())
        {
            return leftScenario->Category() < rightScenario->Category();
        }
        return leftScenario->Name() < rightScenario->Name();
    });
    return copy;
}

std::vector<Scenario*> ScenarioRegistry::Filtered(std::string_view pattern) const
{
    auto allScenarios = All();
    std::vector<Scenario*> matching;
    matching.reserve(allScenarios.size());
    for (auto* scenario : allScenarios)
    {
        if (MatchesGlob(pattern, scenario->FullName()))
        {
            matching.push_back(scenario);
        }
    }
    return matching;
}

}  //  namespace ese::tests
