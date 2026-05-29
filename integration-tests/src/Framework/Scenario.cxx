// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Scenario.hxx"

#include "Framework/ScenarioRegistry.hxx"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>

namespace ese::tests
{

const char* ToString(ScenarioCategory category)
{
    switch (category)
    {
        case ScenarioCategory::Platform:
            return "Platform";
        case ScenarioCategory::Session:
            return "Session";
        case ScenarioCategory::Database:
            return "Database";
        case ScenarioCategory::Schema:
            return "Schema";
        case ScenarioCategory::DataManipulation:
            return "DataManipulation";
        case ScenarioCategory::ColumnType:
            return "ColumnType";
        case ScenarioCategory::Navigation:
            return "Navigation";
        case ScenarioCategory::Transaction:
            return "Transaction";
        case ScenarioCategory::LongValue:
            return "LongValue";
        case ScenarioCategory::MultiValue:
            return "MultiValue";
        case ScenarioCategory::TemporaryTable:
            return "TemporaryTable";
        case ScenarioCategory::Escrow:
            return "Escrow";
        case ScenarioCategory::BackupRestore:
            return "BackupRestore";
        case ScenarioCategory::Recovery:
            return "Recovery";
        case ScenarioCategory::Snapshot:
            return "Snapshot";
        case ScenarioCategory::Maintenance:
            return "Maintenance";
        case ScenarioCategory::Limit:
            return "Limit";
        case ScenarioCategory::Error:
            return "Error";
        case ScenarioCategory::Scale:
            return "Scale";
        case ScenarioCategory::Concurrency:
            return "Concurrency";
        case ScenarioCategory::MultiThreaded:
            return "MultiThreaded";
        case ScenarioCategory::Encryption:
            return "Encryption";
        case ScenarioCategory::Preread:
            return "Preread";
        case ScenarioCategory::Rbs:
            return "Rbs";
        case ScenarioCategory::Replication:
            return "Replication";
        case ScenarioCategory::WideApi:
            return "WideApi";
        case ScenarioCategory::Tracing:
            return "Tracing";
    }
    return "Unknown";
}

const char* ToString(ScenarioTier tier)
{
    switch (tier)
    {
        case ScenarioTier::Smoke:
            return "smoke";
        case ScenarioTier::Regression:
            return "reg";
        case ScenarioTier::LongRunning:
            return "long";
    }
    return "?";
}

const char* ToFullString(ScenarioTier tier)
{
    switch (tier)
    {
        case ScenarioTier::Smoke:
            return "Smoke";
        case ScenarioTier::Regression:
            return "Regression";
        case ScenarioTier::LongRunning:
            return "LongRunning";
    }
    return "Unknown";
}

bool ParseScenarioTier(std::string_view token,
                       ScenarioTier& tier,
                       bool& all)
{
    all = false;
    std::string normalized(token);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (normalized == "smoke")
    {
        tier = ScenarioTier::Smoke;
        return true;
    }
    if (normalized == "reg" || normalized == "regression")
    {
        tier = ScenarioTier::Regression;
        return true;
    }
    if (normalized == "long" || normalized == "long-running"
        || normalized == "longrunning")
    {
        tier = ScenarioTier::LongRunning;
        return true;
    }
    if (normalized == "all")
    {
        all = true;
        return true;
    }
    return false;
}

Scenario::Scenario(ScenarioCategory category,
                   std::string_view name,
                   ScenarioTier tier)
    : _category(category),
      _name(name),
      _tier(tier)
{
    ScenarioRegistry::Instance().Register(this);
}

std::string Scenario::FullName() const
{
    return std::format("{}.{}", ToString(_category), _name);
}

} // namespace ese::tests
