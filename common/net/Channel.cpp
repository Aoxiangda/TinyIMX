#include "common/net/Channel.h"

#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"

#include <utility>

namespace tinyimx {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd) {}

Channel::~Channel() {
    if (event_handling_) {
        LOG_WARN("channel destroyed while handling event"
                 << ", fd=" << fd_);
    }
}

void Channel::Tie(const std::shared_ptr<void>& owner) {
    tie_ = owner;
    tied_ = true;
}

/*
    void Channel::HandleEvent() {
        event_handling_ = true;

        if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
            if (close_callback_) {
                close_callback_();
            }

            event_handling_ = false;
            return;
        }

        if (revents_ & EPOLLERR) {
            if (error_callback_) {
                error_callback_();
            }
        }

        if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
            if (read_callback_) {
                read_callback_();
            }
        }

        if (revents_ & EPOLLOUT) {
            if (write_callback_) {
                write_callback_();
            }
        }

        event_handling_ = false;
    }
*/
void Channel::HandleEvent() {
    if (tied_) {
        std::shared_ptr<void> guard = tie_.lock();

        if (guard) {
            HandleEventWithGuard();
        }

        return;
    }

    HandleEventWithGuard();
}

void Channel::HandleEventWithGuard() {
    event_handling_ = true;

    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (close_callback_) {
            close_callback_();
        }

        event_handling_ = false;
        return;
    }

    if (revents_ & EPOLLERR) {
        if (error_callback_) {
            error_callback_();
        }
    }

    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (read_callback_) {
            read_callback_();
        }
    }

    if (revents_ & EPOLLOUT) {
        if (write_callback_) {
            write_callback_();
        }
    }

    event_handling_ = false;
}

void Channel::SetReadCallback(EventCallback callback) {
    read_callback_ = std::move(callback);
}

void Channel::SetWriteCallback(EventCallback callback) {
    write_callback_ = std::move(callback);
}

void Channel::SetCloseCallback(EventCallback callback) {
    close_callback_ = std::move(callback);
}

void Channel::SetErrorCallback(EventCallback callback) {
    error_callback_ = std::move(callback);
}

void Channel::EnableReading() {
    events_ |= kReadEvent;
    Update();
}

void Channel::DisableReading() {
    events_ &= ~kReadEvent;
    Update();
}

void Channel::EnableWriting() {
    events_ |= kWriteEvent;
    Update();
}

void Channel::DisableWriting() {
    events_ &= ~kWriteEvent;
    Update();
}

void Channel::DisableAll() {
    events_ = kNoneEvent;
    Update();
}

bool Channel::IsReading() const {
    return (events_ & kReadEvent) != 0;
}

bool Channel::IsWriting() const {
    return (events_ & kWriteEvent) != 0;
}

bool Channel::IsNoneEvent() const {
    return events_ == kNoneEvent;
}

int Channel::Fd() const {
    return fd_;
}

uint32_t Channel::Events() const {
    return events_;
}

uint32_t Channel::Revents() const {
    return revents_;
}

void Channel::SetRevents(uint32_t revents) {
    revents_ = revents;
}

bool Channel::IsAddedToLoop() const {
    return added_to_loop_;
}

void Channel::SetAddedToLoop(bool added) {
    added_to_loop_ = added;
}

void Channel::Update() {
    if (loop_ == nullptr) {
        LOG_ERROR("channel update failed: loop is null"
                  << ", fd=" << fd_);
        return;
    }

    loop_->UpdateChannel(this);
}

}  // namespace tinyimx