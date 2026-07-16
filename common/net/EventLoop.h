#pragma once

#include <atomic>
#include <memory>
#include <sys/epoll.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tinyimx {

class Channel;

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    bool IsValid() const;

    void Loop();
    void Quit();

    bool UpdateChannel(Channel* channel);
    bool RemoveChannel(Channel* channel);

    bool IsInLoopThread() const;

private:
    void Wakeup();
    void HandleWakeupRead();

private:
    static constexpr int kInvalidFd = -1;
    static constexpr int kEpollTimeoutMs = 10000;
    static constexpr std::size_t kInitialEventListSize = 16;

    int epoll_fd_{kInvalidFd};
    int wakeup_fd_{kInvalidFd};

    std::atomic<bool> looping_{false};
    std::atomic<bool> quit_{false};

    const std::thread::id owner_thread_id_;

    std::vector<epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;

    std::unique_ptr<Channel> wakeup_channel_;
};

}  // namespace tinyimx