#pragma once
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace tinyimx::capacity {
// The terminal slot at exactly duration is excluded even when conversion to
// integer clock ticks rounds its floating-point offset just below the window.
inline std::uint64_t OfferedRequestCount(double rate,unsigned duration) {
    if(!std::isfinite(rate)||rate<=0||rate>10000||duration==0||duration>7200)
        throw std::invalid_argument("Invalid offered schedule");
    auto slots=rate*duration;
    const auto integer=std::round(slots);
    // Never snap a tiny positive product to zero: slot zero is still offered.
    if(integer>=1 && std::abs(slots-integer)<=1e-8) slots=integer;
    return static_cast<std::uint64_t>(std::ceil(slots));
}
} // namespace tinyimx::capacity
