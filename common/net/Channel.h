#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <sys/epoll.h>

namespace tinyimx {

class EventLoop;

class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    void Tie(const std::shared_ptr<void>& owner);

    void HandleEvent();

    void SetReadCallback(EventCallback callback);
    void SetWriteCallback(EventCallback callback);
    void SetCloseCallback(EventCallback callback);
    void SetErrorCallback(EventCallback callback);

    void EnableReading();
    void DisableReading();

    void EnableWriting();
    void DisableWriting();

    void DisableAll();

    bool IsReading() const;
    bool IsWriting() const;
    bool IsNoneEvent() const;

    int Fd() const;

    uint32_t Events() const;
    uint32_t Revents() const;
    void SetRevents(uint32_t revents);

    bool IsAddedToLoop() const;
    void SetAddedToLoop(bool added);

private:
    void Update();
    void HandleEventWithGuard();

private:
    static constexpr uint32_t kNoneEvent = 0;
    static constexpr uint32_t kReadEvent = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
    static constexpr uint32_t kWriteEvent = EPOLLOUT;

    EventLoop* loop_{nullptr};
    const int fd_{-1};

    uint32_t events_{kNoneEvent};
    uint32_t revents_{kNoneEvent};

    bool added_to_loop_{false};
    bool event_handling_{false};

    std::weak_ptr<void> tie_;
    bool tied_{false};

    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;
};

}  // namespace tinyimx