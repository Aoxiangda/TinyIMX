#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace tinyimx {

class Timer final {
public:
    using Clock = std::chrono::steady_clock;

    using TimePoint = Clock::time_point;

    using Interval = std::chrono::milliseconds;

    using Callback = std::function<void()>;

    Timer(
        Callback callback,
        TimePoint expiration,
        Interval interval,
        std::uint64_t sequence
    );

    ~Timer() = default;

    Timer(const Timer&) = delete;

    Timer& operator=(const Timer&) = delete;

    Timer(Timer&&) = delete;

    Timer& operator=(Timer&&) = delete;

    void Run() const;

    const TimePoint&
    Expiration() const noexcept;

    Interval
    RepeatInterval() const noexcept;

    std::uint64_t
    Sequence() const noexcept;

    bool IsRepeat() const noexcept;

    void Restart(
        TimePoint restart_time
    );

private:
    Callback callback_;

    TimePoint expiration_;

    const Interval interval_;

    const std::uint64_t sequence_{0};
};

}  // namespace tinyimx