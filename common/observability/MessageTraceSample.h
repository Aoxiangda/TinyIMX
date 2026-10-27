#pragma once
#include <atomic>
#include <cstdint>

namespace tinyimx::diagnostics {
// Stateless diagnostic selection shared by gateway, RPC and repository. Mix
// sequential IDs before 1/64 selection to avoid aliasing even/odd clients or
// gateway placement in a round-robin workload. Never used for business logic.
inline bool MessageTraceSelected(std::uint64_t id) noexcept {
    if (id == 0) return false;
    id += 0x9e3779b97f4a7c15ULL;
    id = (id ^ (id >> 30)) * 0xbf58476d1ce4e5b9ULL;
    id = (id ^ (id >> 27)) * 0x94d049bb133111ebULL;
    return ((id ^ (id >> 31)) & 63) == 0;
}

// Reserve8/s for the deterministic MID cohort and8/s for other slow/failing
// diagnostics. Selected requests retain their lane even when they are slow.
// This only bounds emitted diagnostics; it never controls business admission.
class MessageTraceLogBudget final {
public:
    bool Admit(std::uint64_t second, bool selected_mid) noexcept {
        auto& state = states_[selected_mid ? 0 : 1];
        const auto base = second << 8;
        auto old = state.load(std::memory_order_relaxed);
        for (;;) {
            const auto bucket = old & ~std::uint64_t{255};
            if (base < bucket) return false;
            const auto count = base == bucket ? old & 255 : 0;
            if (count >= 8) return false;
            if (state.compare_exchange_weak(old, base | (count + 1),
                                            std::memory_order_relaxed)) return true;
        }
    }
private:
    std::atomic<std::uint64_t> states_[2]{};
};
} // namespace tinyimx::diagnostics
