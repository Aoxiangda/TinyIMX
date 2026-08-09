#pragma once

#include <cstdint>

namespace tinyimx {

class TimerQueue;

class TimerId final {
public:
    TimerId() = default;

    bool IsValid() const noexcept {
        return owner_ != nullptr &&
               sequence_ != 0;
    }

private:
    TimerId(const TimerQueue* owner, std::uint64_t sequence
    ) noexcept
        : owner_(owner),
          sequence_(sequence) {
    }

    const TimerQueue*Owner() const noexcept {
        return owner_;
    }

    std::uint64_t Sequence() const noexcept {
        return sequence_;
    }

private:
    friend class TimerQueue;

    const TimerQueue* owner_{nullptr};

    std::uint64_t sequence_{0};
};

}  // namespace tinyimx