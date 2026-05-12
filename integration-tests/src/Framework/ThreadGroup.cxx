// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/ThreadGroup.hxx"

#include <mutex>
#include <utility>

namespace ese::tests
{

ThreadGroup::~ThreadGroup()
{
    for (auto& worker : _workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

void ThreadGroup::Launch(int count, Worker worker)
{
    _workers.reserve(_workers.size() + static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        _workers.emplace_back([this, worker, index]()
        {
            try
            {
                worker(index);
            }
            catch (...)
            {
                std::scoped_lock<std::mutex> lock(_exceptionLock);
                if (_firstException == nullptr)
                {
                    _firstException = std::current_exception();
                }
            }
        });
    }
}

void ThreadGroup::Join()
{
    for (auto& worker : _workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    _workers.clear();

    if (_firstException != nullptr)
    {
        auto toRethrow = _firstException;
        _firstException = nullptr;
        std::rethrow_exception(toRethrow);
    }
}

}  //  namespace ese::tests
