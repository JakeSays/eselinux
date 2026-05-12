// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/ThreadGroup.hxx"

#include <atomic>

using namespace ese::tests;

EseIntegrationScenario(Concurrency, ThreadGroupLaunchAndJoin)
{
    // Smoke-test the ThreadGroup framework piece without engaging the
    // engine — Phase 5 will fold this into multi-session scenarios.
    static constexpr int WorkerCount = 8;
    std::atomic<int> totalTicks = 0;

    ThreadGroup threadGroup;
    threadGroup.Launch(WorkerCount, [&totalTicks](int /*threadIndex*/)
    {
        for (int tick = 0; tick < 100; ++tick)
        {
            totalTicks.fetch_add(1, std::memory_order_relaxed);
        }
    });
    threadGroup.Join();

    Require(totalTicks.load() == WorkerCount * 100);
}
