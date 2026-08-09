#pragma once

#include "common/net/TimerId.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace tinyimx {

class Channel;
class EventLoop;
class Timer;

class TimerQueue final {
public:
    using Clock =
        std::chrono::steady_clock;

    using TimePoint =
        Clock::time_point;

    using Interval =
        std::chrono::milliseconds;

    using TimerCallback =
        std::function<void()>;

    explicit TimerQueue(
        EventLoop* loop
    );

    ~TimerQueue();

    TimerQueue(
        const TimerQueue&
    ) = delete;

    TimerQueue& operator=(
        const TimerQueue&
    ) = delete;

    TimerQueue(
        TimerQueue&&
    ) = delete;

    TimerQueue& operator=(
        TimerQueue&&
    ) = delete;

    bool IsValid() const noexcept;

    TimerId AddTimer(
        TimerCallback callback,
        TimePoint expiration,
        Interval interval
    );

    bool Cancel(
        TimerId timer_id
    );

private:
    struct Entry {
        TimePoint expiration;
        std::uint64_t sequence{0};
    };

    struct EntryLess {
        bool operator()(
            const Entry& left,
            const Entry& right
        ) const noexcept;
    };

private:
    void AddTimerInLoop(
        TimerCallback callback,
        TimePoint expiration,
        Interval interval,
        std::uint64_t sequence
    );

    void CancelInLoop(
        std::uint64_t sequence
    );

    void HandleRead();

    void ResetTimerfd(
        TimePoint expiration
    );

    void ResetTimerfdToNext();

    void DisarmTimerfd();

private:
    static constexpr int
        kInvalidFd = -1;

    EventLoop* loop_{nullptr};

    int timer_fd_{kInvalidFd};

    std::unique_ptr<Channel>
        timer_channel_;

    std::set<Entry, EntryLess>
        schedule_;

    std::unordered_map<
        std::uint64_t,
        std::unique_ptr<Timer>
    > timers_;

    std::atomic<std::uint64_t>
        next_sequence_{1};

    bool calling_expired_timers_{
        false
    };

    std::unordered_set<
        std::uint64_t
    > canceling_sequences_;

    std::mutex pending_mutex_;

    std::unordered_set<
        std::uint64_t
    > pending_add_sequences_;

    std::unordered_set<
        std::uint64_t
    > pending_cancel_sequences_;
};

}  // namespace tinyimx