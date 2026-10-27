#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
namespace tinyimx::message::local {
class LocalEventSignal final {
public:
    void Notify(){std::lock_guard lock(mutex_);++version_;cv_.notify_all();}
    std::uint64_t Version(){std::lock_guard lock(mutex_);return version_;}
    std::uint64_t WaitUntil(std::uint64_t observed,std::chrono::steady_clock::time_point deadline){
        std::unique_lock lock(mutex_);cv_.wait_until(lock,deadline,[&]{return version_!=observed;});return version_;
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::uint64_t version_{0};
};
}
