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

//  Functional grouping — names the engine area the scenario is
//  testing.  Encoded in the first half of FullName ("Schema.Foo").
//  Independent of tier — a Schema scenario can sit at any tier.
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
    MultiThreaded,
    Encryption,
    Preread,
    Rbs,
    Replication,
    WideApi,
    Tracing,
};

//  Tier — depth/intent of the test.  Orthogonal to Category.
//  Smoke      — fast public-API exercise, asserts contract +
//               immediate round-trip.  Default tier.
//  Regression — heavier scenarios designed to catch refactor-induced
//               regressions in internal heuristics (B-tree split
//               policy, freelist coalescing, concurrency, etc.).
//               Larger row counts, randomized patterns, multi-cycle
//               state exercise.
//  LongRunning — minutes-to-hours stress.  Opt-in.
enum class ScenarioTier
{
    Smoke,
    Regression,
    LongRunning,
};

const char* ToString(ScenarioCategory category);

//  ToString returns the canonical alias used in --list and on the
//  command line: "smoke", "reg", "long".  Use ToFullString for the
//  human-friendly long form ("Regression", "LongRunning").
const char* ToString(ScenarioTier tier);
const char* ToFullString(ScenarioTier tier);

//  Parse a single tier token (alias or canonical name, case-
//  insensitive).  Accepted forms:
//    smoke
//    reg | regression
//    long | long-running | longrunning
//    all  (caller expands to the full set)
//  Returns true on success and writes to tier (unless the token
//  is "all", in which case all is set and tier is undefined).
bool ParseScenarioTier(std::string_view token,
                       ScenarioTier& tier,
                       bool& all);

class Scenario
{
public:
    Scenario(ScenarioCategory category,
             std::string_view name,
             ScenarioTier tier);
    virtual ~Scenario() = default;

    Scenario(const Scenario&) = delete;
    Scenario& operator=(const Scenario&) = delete;

    ScenarioCategory Category() const
    {
        return _category;
    }

    ScenarioTier Tier() const
    {
        return _tier;
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
    ScenarioTier _tier;
};

} // namespace ese::tests

// EseIntegrationScenario(Category, Name, Tier) { body } — declares
// a scenario in the calling translation unit.  Tier is one of the
// ScenarioTier enumerators (Smoke / Regression / LongRunning).
// Generates:
// 1. A free function holding the user-supplied body.
// 2. A subclass of Scenario whose Run() forwards to that function.
// 3. A file-local instance of the subclass — its base-class ctor
//    self-registers in the global ScenarioRegistry at static-init
//    time.
#define EseIntegrationScenario(category, name, tier) \
    static void EseIntegrationScenarioBody_##category##_##name(); \
    namespace \
    { \
    struct EseIntegrationScenarioRunner_##category##_##name : public ::ese::tests::Scenario \
    { \
        EseIntegrationScenarioRunner_##category##_##name() \
            : ::ese::tests::Scenario(::ese::tests::ScenarioCategory::category, \
                                     #name, \
                                     ::ese::tests::ScenarioTier::tier) \
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
