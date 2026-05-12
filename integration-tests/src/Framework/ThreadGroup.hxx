// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// ThreadGroup — std::thread fan-out + join with first-failure capture.
// Concurrency scenarios spawn N worker threads, each running a
// worker(threadIndex) callable. If any thread throws, the exception
// is captured and re-thrown on Join().

#pragma once

#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ese::tests
{

class ThreadGroup
{
public:
    using Worker = std::function<void(int threadIndex)>;

    ThreadGroup() = default;
    ~ThreadGroup();

    ThreadGroup(const ThreadGroup&) = delete;
    ThreadGroup& operator=(const ThreadGroup&) = delete;

    // Spawns `count` worker threads each invoking `worker(threadIndex)`.
    void Launch(int count, Worker worker);

    // Waits for every worker to finish. If any captured an exception,
    // re-throws the first one.
    void Join();

private:
    std::vector<std::thread> _workers;
    std::exception_ptr _firstException = nullptr;
    std::mutex _exceptionLock;
};

} // namespace ese::tests
