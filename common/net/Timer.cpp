#include "common/net/Timer.h"

#include <utility>

namespace tinyimx {

Timer::Timer(
    Callback callback,
    TimePoint expiration,
    Interval interval,
    std::uint64_t sequence
)
    : callback_(std::move(callback)),
      expiration_(expiration),
      interval_(interval),
      sequence_(sequence) {
}

void Timer::Run() const {
    if (callback_) {
        callback_();
    }
}

const Timer::TimePoint&
Timer::Expiration() const noexcept {
    return expiration_;
}

Timer::Interval
Timer::RepeatInterval() const noexcept {
    return interval_;
}

std::uint64_t
Timer::Sequence() const noexcept {
    return sequence_;
}

bool Timer::IsRepeat() const noexcept {
    return interval_ >
           Interval::zero();
}

void Timer::Restart(
    TimePoint restart_time
) {
    if (!IsRepeat()) {
        return;
    }

    expiration_ =
        restart_time +
        interval_;
}

}  // namespace tinyimx