#include "common/net/EventLoop.h"

#include "common/logging/LogMacros.h"
#include "common/net/Channel.h"
#include "common/net/TimerQueue.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>

namespace tinyimx {
namespace {

std::string ErrnoString() {
    return std::strerror(errno);
}

}  // namespace

EventLoop::EventLoop()
    : epoll_fd_(
          ::epoll_create1(
              EPOLL_CLOEXEC
          )
      ),
      wakeup_fd_(
          ::eventfd(
              0,
              EFD_NONBLOCK |
                  EFD_CLOEXEC
          )
      ),
      owner_thread_id_(
          std::this_thread::get_id()
      ),
      events_(
          kInitialEventListSize
      ) {
    if (epoll_fd_ < 0) {
        LOG_ERROR(
            "epoll_create1 failed"
            << ", error="
            << ErrnoString()
        );

        return;
    }

    if (wakeup_fd_ < 0) {
        LOG_ERROR(
            "eventfd failed"
            << ", error="
            << ErrnoString()
        );

        return;
    }

    wakeup_channel_ =
        std::make_unique<Channel>(
            this,
            wakeup_fd_
        );

    wakeup_channel_->SetReadCallback(
        [this]() {
            HandleWakeupRead();
        }
    );

    wakeup_channel_->EnableReading();

    timer_queue_ =
        std::make_unique<TimerQueue>(
            this
        );

    if (!timer_queue_->IsValid()) {
        LOG_ERROR(
            "event loop timer queue "
            "initialization failed"
        );

        timer_queue_.reset();
    }

    LOG_INFO(
        "event loop created"
        << ", epoll_fd=" << epoll_fd_
        << ", wakeup_fd="
        << wakeup_fd_
    );
}

EventLoop::~EventLoop() {
    quit_.store(
        true,
        std::memory_order_relaxed
    );

    if (timer_queue_) {
        timer_queue_.reset();
    }

    if (wakeup_channel_) {
        wakeup_channel_->DisableAll();

        RemoveChannel(
            wakeup_channel_.get()
        );

        wakeup_channel_.reset();
    }

    if (wakeup_fd_ != kInvalidFd) {
        ::close(wakeup_fd_);
        wakeup_fd_ = kInvalidFd;
    }

    if (epoll_fd_ != kInvalidFd) {
        ::close(epoll_fd_);
        epoll_fd_ = kInvalidFd;
    }

    LOG_INFO(
        "event loop destroyed"
    );
}

bool EventLoop::IsValid() const {
    return epoll_fd_ != kInvalidFd &&
           wakeup_fd_ != kInvalidFd;
}

void EventLoop::Loop() {
    if (!IsValid()) {
        LOG_ERROR(
            "event loop start failed: "
            "invalid loop"
        );

        return;
    }

    if (looping_.exchange(true)) {
        LOG_ERROR(
            "event loop already running"
        );

        return;
    }

    quit_.store(
        false,
        std::memory_order_relaxed
    );

    LOG_INFO(
        "event loop started"
    );

    while (!quit_.load(
        std::memory_order_relaxed
    )) {
        const int event_count =
            ::epoll_wait(
                epoll_fd_,
                events_.data(),
                static_cast<int>(
                    events_.size()
                ),
                kEpollTimeoutMs
            );

        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }

            LOG_ERROR(
                "epoll_wait failed"
                << ", error="
                << ErrnoString()
            );

            break;
        }

        for (int i = 0;
             i < event_count;
             ++i) {
            auto* channel =
                static_cast<Channel*>(
                    events_[i].
                        data.ptr
                );

            if (channel == nullptr) {
                continue;
            }

            channel->SetRevents(
                events_[i].events
            );

            channel->HandleEvent();
        }

        DoPendingFunctors();

        if (static_cast<std::size_t>(
                event_count
            ) ==
            events_.size()) {
            events_.resize(
                events_.size() * 2
            );
        }
    }

    looping_.store(
        false,
        std::memory_order_relaxed
    );

    LOG_INFO(
        "event loop stopped"
    );
}

void EventLoop::Quit() {
    quit_.store(
        true,
        std::memory_order_relaxed
    );

    if (!IsInLoopThread()) {
        Wakeup();
    }
}

void EventLoop::RunInLoop(
    Functor callback
) {
    if (!callback) {
        return;
    }

    if (IsInLoopThread()) {
        callback();
        return;
    }

    QueueInLoop(
        std::move(callback)
    );
}

void EventLoop::QueueInLoop(
    Functor callback
) {
    if (!callback) {
        return;
    }

    {
        std::lock_guard<std::mutex>
            lock(
                pending_functors_mutex_
            );

        pending_functors_.push_back(
            std::move(callback)
        );
    }

    if (!IsInLoopThread() ||
        calling_pending_functors_.load(
            std::memory_order_relaxed
        )) {
        Wakeup();
    }
}

TimerId EventLoop::RunAfter(
    std::chrono::milliseconds delay,
    Functor callback
) {
    if (!callback ||
        delay <
            std::chrono::milliseconds::
                zero()) {
        return {};
    }

    if (!timer_queue_ ||
        !timer_queue_->IsValid()) {
        LOG_ERROR(
            "run after failed: "
            "timer queue is invalid"
        );

        return {};
    }

    const auto expiration =
        std::chrono::steady_clock::
            now() +
        delay;

    return timer_queue_->AddTimer(
        std::move(callback),
        expiration,
        std::chrono::milliseconds::
            zero()
    );
}

TimerId EventLoop::RunEvery(
    std::chrono::milliseconds interval,
    Functor callback
) {
    if (!callback ||
        interval <=
            std::chrono::milliseconds::
                zero()) {
        return {};
    }

    if (!timer_queue_ ||
        !timer_queue_->IsValid()) {
        LOG_ERROR(
            "run every failed: "
            "timer queue is invalid"
        );

        return {};
    }

    const auto expiration =
        std::chrono::steady_clock::
            now() +
        interval;

    return timer_queue_->AddTimer(
        std::move(callback),
        expiration,
        interval
    );
}

bool EventLoop::Cancel(
    TimerId timer_id
) {
    if (!timer_queue_ ||
        !timer_queue_->IsValid()) {
        return false;
    }

    return timer_queue_->Cancel(
        timer_id
    );
}

bool EventLoop::UpdateChannel(
    Channel* channel
) {
    if (channel == nullptr) {
        LOG_ERROR(
            "update channel failed: "
            "channel is null"
        );

        return false;
    }

    if (!IsValid()) {
        LOG_ERROR(
            "update channel failed: "
            "invalid loop"
            << ", fd="
            << channel->Fd()
        );

        return false;
    }

    const int fd =
        channel->Fd();

    if (fd < 0) {
        LOG_ERROR(
            "update channel failed: "
            "invalid fd"
        );

        return false;
    }

    epoll_event event {};

    event.events =
        channel->Events();

    event.data.ptr =
        channel;

    if (!channel->IsAddedToLoop()) {
        channels_[fd] =
            channel;

        if (channel->IsNoneEvent()) {
            return true;
        }

        if (::epoll_ctl(
                epoll_fd_,
                EPOLL_CTL_ADD,
                fd,
                &event
            ) != 0) {
            LOG_ERROR(
                "epoll_ctl add failed"
                << ", fd=" << fd
                << ", events="
                << event.events
                << ", error="
                << ErrnoString()
            );

            return false;
        }

        channel->SetAddedToLoop(
            true
        );

        return true;
    }

    if (channel->IsNoneEvent()) {
        if (::epoll_ctl(
                epoll_fd_,
                EPOLL_CTL_DEL,
                fd,
                nullptr
            ) != 0) {
            LOG_ERROR(
                "epoll_ctl del failed"
                << ", fd=" << fd
                << ", error="
                << ErrnoString()
            );

            return false;
        }

        channel->SetAddedToLoop(
            false
        );

        return true;
    }

    if (::epoll_ctl(
            epoll_fd_,
            EPOLL_CTL_MOD,
            fd,
            &event
        ) != 0) {
        LOG_ERROR(
            "epoll_ctl mod failed"
            << ", fd=" << fd
            << ", events="
            << event.events
            << ", error="
            << ErrnoString()
        );

        return false;
    }

    return true;
}

bool EventLoop::RemoveChannel(
    Channel* channel
) {
    if (channel == nullptr) {
        return false;
    }

    const int fd =
        channel->Fd();

    auto iter =
        channels_.find(fd);

    if (iter !=
        channels_.end()) {
        channels_.erase(iter);
    }

    if (IsValid() &&
        channel->
            IsAddedToLoop()) {
        if (::epoll_ctl(
                epoll_fd_,
                EPOLL_CTL_DEL,
                fd,
                nullptr
            ) != 0) {
            LOG_ERROR(
                "epoll_ctl remove failed"
                << ", fd=" << fd
                << ", error="
                << ErrnoString()
            );

            return false;
        }

        channel->SetAddedToLoop(
            false
        );
    }

    return true;
}

bool EventLoop::IsInLoopThread() const {
    return
        std::this_thread::get_id() ==
        owner_thread_id_;
}

void EventLoop::Wakeup() {
    if (wakeup_fd_ ==
        kInvalidFd) {
        return;
    }

    std::uint64_t value = 1;

    const ssize_t n =
        ::write(
            wakeup_fd_,
            &value,
            sizeof(value)
        );

    if (n !=
        static_cast<ssize_t>(
            sizeof(value)
        )) {
        if (errno == EAGAIN ||
            errno == EWOULDBLOCK) {
            return;
        }

        LOG_ERROR(
            "event loop wakeup "
            "write failed"
            << ", error="
            << ErrnoString()
        );
    }
}

void EventLoop::HandleWakeupRead() {
    std::uint64_t value = 0;

    const ssize_t n =
        ::read(
            wakeup_fd_,
            &value,
            sizeof(value)
        );

    if (n < 0) {
        if (errno == EAGAIN ||
            errno == EWOULDBLOCK) {
            return;
        }

        LOG_ERROR(
            "event loop wakeup "
            "read failed"
            << ", error="
            << ErrnoString()
        );
    }
}

void EventLoop::DoPendingFunctors() {
    std::vector<Functor>
        functors;

    calling_pending_functors_.store(
        true,
        std::memory_order_relaxed
    );

    {
        std::lock_guard<std::mutex>
            lock(
                pending_functors_mutex_
            );

        functors.swap(
            pending_functors_
        );
    }

    for (auto& functor :
         functors) {
        if (functor) {
            functor();
        }
    }

    calling_pending_functors_.store(
        false,
        std::memory_order_relaxed
    );
}

}  // namespace tinyimx