#pragma once

#include <chrono>
#if defined(__linux__)
#include <time.h>
#endif

namespace tinyimx {

// Dedicated clock domain for multi-second replay cooldowns. Do not use this
// clock for RPC deadlines or latency measurements. On Linux the coarse
// monotonic clock avoids expensive virtualized precise-clock reads.
struct DeliveryRecoveryClock {
    using rep = std::chrono::nanoseconds::rep;
    using period = std::chrono::nanoseconds::period;
    using duration = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<DeliveryRecoveryClock>;
    static constexpr bool is_steady = true;

    static duration Uncertainty() noexcept {
#if defined(__linux__) && defined(CLOCK_MONOTONIC_COARSE)
        static const duration resolution = [] {
            timespec value{};
            if (::clock_getres(CLOCK_MONOTONIC_COARSE, &value) == 0) {
                const auto result = std::chrono::seconds{value.tv_sec} +
                                    std::chrono::nanoseconds{value.tv_nsec};
                if (result > duration::zero() && result <= std::chrono::milliseconds{10})
                    return duration{result};
            }
            return duration::zero();
        }();
        return resolution;
#else
        return duration::zero();
#endif
    }

    static time_point now() noexcept {
#if defined(__linux__)
        timespec value{};
#if defined(CLOCK_MONOTONIC_COARSE)
        if (Uncertainty() != duration::zero() &&
            ::clock_gettime(CLOCK_MONOTONIC_COARSE, &value) == 0)
            return FromTimespec(value);
#endif
        // Same native monotonic epoch on Linux if coarse reads are unavailable.
        if (::clock_gettime(CLOCK_MONOTONIC, &value) == 0) return FromTimespec(value);
#endif
        return time_point{std::chrono::duration_cast<duration>(
            std::chrono::steady_clock::now().time_since_epoch())};
    }

private:
#if defined(__linux__)
    static time_point FromTimespec(const timespec& value) noexcept {
        return time_point{std::chrono::seconds{value.tv_sec} +
                          std::chrono::nanoseconds{value.tv_nsec}};
    }
#endif
};

}  // namespace tinyimx
