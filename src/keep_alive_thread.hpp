#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace workflow
{

struct KeepAliveThread
{
    std::chrono::milliseconds interval;
    std::function<void()> ping;

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::thread thread;

    KeepAliveThread(std::chrono::milliseconds interval_, std::function<void()> ping_)
        : interval(interval_), ping(std::move(ping_)), thread([this] { run(); })
    {
    }

    ~KeepAliveThread()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_all();
        thread.join();
    }

    void run()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!done)
        {
            cv.wait_for(lock, interval, [this] { return done; });
            if (!done)
            {
                try
                {
                    ping();
                }
                catch (...)
                {
                }
            }
        }
    }
};

} // namespace workflow
