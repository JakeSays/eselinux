// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Scenario — abstract base for every integration test. Each concrete
// scenario subclasses Scenario, registers itself in the global
// ScenarioRegistry at static-init time, and implements Run().
// Failures bubble out as ScenarioFailure exceptions (see Check.hxx).
//
// Authors don't write subclasses by hand — the EseIntegrationScenario
// macro at the bottom of this header generates the boilerplate.

#pragma once

#include <string>
#include <string_view>

namespace ese::tests
{

enum class ScenarioCategory
{
    Platform,
    Session,
    Database,
    Schema,
    DataManipulation,
    ColumnType,
    Navigation,
    Transaction,
    LongValue,
    MultiValue,
    TemporaryTable,
    Escrow,
    BackupRestore,
    Recovery,
    Snapshot,
    Maintenance,
    Limit,
    Error,
    Scale,
    Concurrency,
    Encryption,
    Preread,
};

const char* ToString(ScenarioCategory category);

class Scenario
{
public:
    Scenario(ScenarioCategory category, std::string_view name);
    virtual ~Scenario() = default;

    Scenario(const Scenario&) = delete;
    Scenario& operator=(const Scenario&) = delete;

    ScenarioCategory Category() const
    {
        return _category;
    }

    const std::string& Name() const
    {
        return _name;
    }

    // "<Category>.<Name>", used for pattern matching and reporting.
    std::string FullName() const;

    // Implemented by the subclass via the macro below.
    virtual void Run() = 0;

private:
    ScenarioCategory _category;
    std::string _name;
};

} // namespace ese::tests

// EseIntegrationScenario(Category, Name) { body } — declares a scenario
// in the calling translation unit. Generates:
// 1. A free function holding the user-supplied body.
// 2. A subclass of Scenario whose Run() forwards to that function.
// 3. A file-local instance of the subclass — its base-class ctor
// self-registers in the global ScenarioRegistry at static-init time.
#define EseIntegrationScenario(category, name) \
    static void EseIntegrationScenarioBody_##category##_##name(); \
    namespace \
    { \
    struct EseIntegrationScenarioRunner_##category##_##name : public ::ese::tests::Scenario \
    { \
        EseIntegrationScenarioRunner_##category##_##name() \
            : ::ese::tests::Scenario(::ese::tests::ScenarioCategory::category, #name) \
        { \
        } \
        void Run() override \
        { \
            EseIntegrationScenarioBody_##category##_##name(); \
        } \
    }; \
    static EseIntegrationScenarioRunner_##category##_##name \
        EseIntegrationScenarioInstance_##category##_##name; \
    } \
    static void EseIntegrationScenarioBody_##category##_##name()
