#include "common/net/TimerQueue.h"

#include "common/logging/LogMacros.h"
#include "common/net/Channel.h"
#include "common/net/EventLoop.h"
#include "common/net/Timer.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace tinyimx {
namespace {

std::string ErrnoString() {
    return std::strerror(errno);
}

}  // namespace

TimerQueue::TimerQueue(
    EventLoop* loop
)
    : loop_(loop) {
    if (loop_ == nullptr ||
        !loop_->IsValid()) {
        LOG_ERROR(
            "timer queue create failed: "
            "invalid event loop"
        );

        return;
    }

    timer_fd_ =
        ::timerfd_create(
            CLOCK_MONOTONIC,
            TFD_NONBLOCK |
                TFD_CLOEXEC
        );

    if (timer_fd_ < 0) {
        LOG_ERROR(
            "timerfd_create failed"
            << ", error="
            << ErrnoString()
        );

        return;
    }

    timer_channel_ =
        std::make_unique<Channel>(
            loop_,
            timer_fd_
        );

    timer_channel_->SetReadCallback(
        [this]() {
            HandleRead();
        }
    );

    timer_channel_->SetErrorCallback(
        [this]() {
            LOG_ERROR(
                "timer channel error"
                << ", fd="
                << timer_fd_
            );
        }
    );

    timer_channel_->EnableReading();

    LOG_INFO(
        "timer queue created"
        << ", timer_fd="
        << timer_fd_
    );
}

TimerQueue::~TimerQueue() {
    schedule_.clear();
    timers_.clear();
    canceling_sequences_.clear();

    {
        std::lock_guard<std::mutex>
            lock(pending_mutex_);

        pending_add_sequences_.clear();
        pending_cancel_sequences_.clear();
    }

    if (timer_channel_) {
        timer_channel_->DisableAll();

        if (loop_ != nullptr) {
            loop_->RemoveChannel(
                timer_channel_.get()
            );
        }

        timer_channel_.reset();
    }

    if (timer_fd_ != kInvalidFd) {
        ::close(timer_fd_);
        timer_fd_ = kInvalidFd;
    }

    LOG_INFO(
        "timer queue destroyed"
    );
}

bool TimerQueue::EntryLess::operator()(
    const Entry& left,
    const Entry& right
) const noexcept {
    if (left.expiration <
        right.expiration) {
        return true;
    }

    if (right.expiration <
        left.expiration) {
        return false;
    }

    return left.sequence <
           right.sequence;
}

bool TimerQueue::IsValid() const noexcept {
    return timer_fd_ != kInvalidFd &&
           timer_channel_ != nullptr;
}

TimerId TimerQueue::AddTimer(
    TimerCallback callback,
    TimePoint expiration,
    Interval interval
) {
    if (!callback ||
        loop_ == nullptr ||
        !IsValid()) {
        return {};
    }

    const std::uint64_t sequence =
        next_sequence_.fetch_add(
            1,
            std::memory_order_relaxed
        );

    {
        std::lock_guard<std::mutex>
            lock(pending_mutex_);

        pending_add_sequences_.insert(
            sequence
        );
    }

    loop_->RunInLoop(
        [
            this,
            callback =
                std::move(callback),
            expiration,
            interval,
            sequence
        ]() mutable {
            AddTimerInLoop(
                std::move(callback),
                expiration,
                interval,
                sequence
            );
        }
    );

    return TimerId(
        this,
        sequence
    );
}

bool TimerQueue::Cancel(
    TimerId timer_id
) {
    if (!timer_id.IsValid() ||
        timer_id.Owner() != this ||
        loop_ == nullptr ||
        !IsValid()) {
        return false;
    }

    const std::uint64_t sequence =
        timer_id.Sequence();

    {
        std::lock_guard<std::mutex>
            lock(pending_mutex_);

        if (pending_add_sequences_.find(
                sequence
            ) !=
            pending_add_sequences_.end()) {
            pending_cancel_sequences_.insert(
                sequence
            );
        }
    }

    loop_->RunInLoop(
        [this, sequence]() {
            CancelInLoop(sequence);
        }
    );

    return true;
}

void TimerQueue::AddTimerInLoop(
    TimerCallback callback,
    TimePoint expiration,
    Interval interval,
    std::uint64_t sequence
) {
    if (loop_ == nullptr ||
        !loop_->IsInLoopThread()) {
        LOG_ERROR(
            "add timer called outside "
            "event loop thread"
            << ", sequence="
            << sequence
        );

        return;
    }

    bool canceled_before_add = false;

    {
        std::lock_guard<std::mutex>
            lock(pending_mutex_);

        pending_add_sequences_.erase(
            sequence
        );

        auto cancel_iter =
            pending_cancel_sequences_.find(
                sequence
            );

        if (cancel_iter !=
            pending_cancel_sequences_.end()) {
            canceled_before_add = true;

            pending_cancel_sequences_.erase(
                cancel_iter
            );
        }
    }

    if (canceled_before_add) {
        return;
    }

    Entry entry {
        expiration,
        sequence
    };

    const bool earliest_changed =
        schedule_.empty() ||
        EntryLess{}(
            entry,
            *schedule_.begin()
        );

    auto timer =
        std::make_unique<Timer>(
            std::move(callback),
            expiration,
            interval,
            sequence
        );

    const auto timer_result =
        timers_.emplace(
            sequence,
            std::move(timer)
        );

    if (!timer_result.second) {
        LOG_ERROR(
            "add timer failed: "
            "duplicate sequence"
            << ", sequence="
            << sequence
        );

        return;
    }

    const auto schedule_result =
        schedule_.insert(entry);

    if (!schedule_result.second) {
        timers_.erase(sequence);

        LOG_ERROR(
            "add timer schedule failed"
            << ", sequence="
            << sequence
        );

        return;
    }

    if (earliest_changed) {
        ResetTimerfd(expiration);
    }
}

void TimerQueue::CancelInLoop(
    std::uint64_t sequence
) {
    if (loop_ == nullptr ||
        !loop_->IsInLoopThread()) {
        LOG_ERROR(
            "cancel timer called outside "
            "event loop thread"
            << ", sequence="
            << sequence
        );

        return;
    }

    auto timer_iter =
        timers_.find(sequence);

    if (timer_iter !=
        timers_.end()) {
        const Entry entry {
            timer_iter->second->
                Expiration(),
            sequence
        };

        const bool earliest_removed =
            !schedule_.empty() &&
            schedule_.begin()->
                sequence ==
                sequence;

        schedule_.erase(entry);
        timers_.erase(timer_iter);

        if (earliest_removed) {
            ResetTimerfdToNext();
        }

        return;
    }

    if (calling_expired_timers_) {
        canceling_sequences_.insert(
            sequence
        );
    }
}

void TimerQueue::HandleRead() {
    if (loop_ == nullptr ||
        !loop_->IsInLoopThread()) {
        LOG_ERROR(
            "timer read called outside "
            "event loop thread"
        );

        return;
    }

    std::uint64_t expiration_count = 0;

    const ssize_t n =
        ::read(
            timer_fd_,
            &expiration_count,
            sizeof(expiration_count)
        );

    if (n != static_cast<ssize_t>(
                 sizeof(expiration_count)
             )) {
        if (n < 0 &&
            (errno == EAGAIN ||
             errno == EWOULDBLOCK)) {
            return;
        }

        LOG_ERROR(
            "timerfd read failed"
            << ", fd=" << timer_fd_
            << ", error="
            << ErrnoString()
        );

        return;
    }

    (void)expiration_count;

    const TimePoint now =
        Clock::now();

    const Entry upper_bound {
        now,
        std::numeric_limits<
            std::uint64_t
        >::max()
    };

    const auto expired_end =
        schedule_.upper_bound(
            upper_bound
        );

    std::vector<
        std::unique_ptr<Timer>
    > expired_timers;

    for (auto iter =
             schedule_.begin();
         iter != expired_end;
         ++iter) {
        auto timer_iter =
            timers_.find(
                iter->sequence
            );

        if (timer_iter ==
            timers_.end()) {
            continue;
        }

        expired_timers.push_back(
            std::move(
                timer_iter->second
            )
        );

        timers_.erase(
            timer_iter
        );
    }

    schedule_.erase(
        schedule_.begin(),
        expired_end
    );

    calling_expired_timers_ = true;

    canceling_sequences_.clear();

    for (const auto& timer :
         expired_timers) {
        if (!timer) {
            continue;
        }

        if (canceling_sequences_.find(
                timer->Sequence()
            ) !=
            canceling_sequences_.end()) {
            continue;
        }

        timer->Run();
    }

    calling_expired_timers_ = false;

    const TimePoint restart_time =
        Clock::now();

    for (auto& timer :
         expired_timers) {
        if (!timer ||
            !timer->IsRepeat()) {
            continue;
        }

        if (canceling_sequences_.find(
                timer->Sequence()
            ) !=
            canceling_sequences_.end()) {
            continue;
        }

        timer->Restart(
            restart_time
        );

        const Entry entry {
            timer->Expiration(),
            timer->Sequence()
        };

        const std::uint64_t sequence =
            timer->Sequence();

        const auto timer_result =
            timers_.emplace(
                sequence,
                std::move(timer)
            );

        if (!timer_result.second) {
            LOG_ERROR(
                "repeat timer restart failed"
                << ", sequence="
                << sequence
            );

            continue;
        }

        const auto schedule_result =
            schedule_.insert(entry);

        if (!schedule_result.second) {
            timers_.erase(sequence);

            LOG_ERROR(
                "repeat timer schedule failed"
                << ", sequence="
                << sequence
            );
        }
    }

    canceling_sequences_.clear();

    ResetTimerfdToNext();
}

void TimerQueue::ResetTimerfd(
    TimePoint expiration
) {
    if (timer_fd_ ==
        kInvalidFd) {
        return;
    }

    auto delay =
        expiration -
        Clock::now();

    if (delay <=
        Clock::duration::zero()) {
        delay =
            std::chrono::nanoseconds(1);
    }

    const auto seconds_part =
        std::chrono::duration_cast<
            std::chrono::seconds
        >(delay);

    const auto nanoseconds_part =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(
            delay -
            seconds_part
        );

    itimerspec new_value {};

    new_value.it_value.tv_sec =
        static_cast<
            decltype(
                new_value.it_value.tv_sec
            )
        >(
            seconds_part.count()
        );

    new_value.it_value.tv_nsec =
        static_cast<
            decltype(
                new_value.it_value.tv_nsec
            )
        >(
            nanoseconds_part.count()
        );

    if (::timerfd_settime(
            timer_fd_,
            0,
            &new_value,
            nullptr
        ) != 0) {
        LOG_ERROR(
            "timerfd_settime failed"
            << ", fd=" << timer_fd_
            << ", error="
            << ErrnoString()
        );
    }
}

void TimerQueue::ResetTimerfdToNext() {
    if (schedule_.empty()) {
        DisarmTimerfd();
        return;
    }

    ResetTimerfd(
        schedule_.begin()->
            expiration
    );
}

void TimerQueue::DisarmTimerfd() {
    if (timer_fd_ ==
        kInvalidFd) {
        return;
    }

    itimerspec new_value {};

    if (::timerfd_settime(
            timer_fd_,
            0,
            &new_value,
            nullptr
        ) != 0) {
        LOG_ERROR(
            "timerfd disarm failed"
            << ", fd=" << timer_fd_
            << ", error="
            << ErrnoString()
        );
    }
}

}  // namespace tinyimx