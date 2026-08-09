#pragma once

#include "common/net/TimerId.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <sys/epoll.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tinyimx {

class Channel;
class TimerQueue;

class EventLoop {
public:
    using Functor =
        std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(
        const EventLoop&
    ) = delete;

    EventLoop& operator=(
        const EventLoop&
    ) = delete;

    bool IsValid() const;

    void Loop();
    void Quit();

    void RunInLoop(
        Functor callback
    );

    void QueueInLoop(
        Functor callback
    );

    TimerId RunAfter(
        std::chrono::milliseconds delay,
        Functor callback
    );

    TimerId RunEvery(
        std::chrono::milliseconds interval,
        Functor callback
    );

    bool Cancel(
        TimerId timer_id
    );

    bool UpdateChannel(
        Channel* channel
    );

    bool RemoveChannel(
        Channel* channel
    );

    bool IsInLoopThread() const;

private:
    void Wakeup();
    void HandleWakeupRead();

    void DoPendingFunctors();

private:
    static constexpr int
        kInvalidFd = -1;

    static constexpr int
        kEpollTimeoutMs = 10000;

    static constexpr std::size_t
        kInitialEventListSize = 16;

    int epoll_fd_{kInvalidFd};
    int wakeup_fd_{kInvalidFd};

    std::atomic<bool>
        looping_{false};

    std::atomic<bool>
        quit_{false};

    std::atomic<bool>
        calling_pending_functors_{
            false
        };

    const std::thread::id
        owner_thread_id_;

    std::vector<epoll_event>
        events_;

    std::unordered_map<
        int,
        Channel*
    > channels_;

    std::mutex
        pending_functors_mutex_;

    std::vector<Functor>
        pending_functors_;

    std::unique_ptr<Channel>
        wakeup_channel_;

    std::unique_ptr<TimerQueue>
        timer_queue_;
};

}  // namespace tinyimx