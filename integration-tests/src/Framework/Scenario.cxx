// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Scenario.hxx"

#include "Framework/ScenarioRegistry.hxx"

#include <format>

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
    }
    return "Unknown";
}

Scenario::Scenario(ScenarioCategory category, std::string_view name)
    : _category(category),
      _name(name)
{
    ScenarioRegistry::Instance().Register(this);
}

std::string Scenario::FullName() const
{
    return std::format("{}.{}", ToString(_category), _name);
}

}  //  namespace ese::tests
