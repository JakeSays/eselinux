// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// ScenarioRegistry — process-wide singleton holding every Scenario
// declared via EseIntegrationScenario. Scenarios self-register from
// their base-class constructor at static-init time. Main() asks for
// the full list, applies --filter glob matching, runs the survivors.

#pragma once

#include <string_view>
#include <vector>

namespace ese::tests
{

class Scenario;

class ScenarioRegistry
{
public:
    static ScenarioRegistry& Instance();

    void Register(Scenario* scenario);

    // All registered scenarios, sorted by Category then Name.
    std::vector<Scenario*> All() const;

    // Filter by a glob-style pattern (* and ? supported) matched against
    // the scenario's FullName(). Empty pattern returns everything.
    std::vector<Scenario*> Filtered(std::string_view pattern) const;

private:
    ScenarioRegistry() = default;
    ScenarioRegistry(const ScenarioRegistry&) = delete;
    ScenarioRegistry& operator=(const ScenarioRegistry&) = delete;

    std::vector<Scenario*> _scenarios;
};

} // namespace ese::tests
