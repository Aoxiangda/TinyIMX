#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>

namespace tinyimx {

// P1-B R1: retains the obligation to START the existing private replay work.
// This is NOT an ACK/convergence tracker, a worker pool, or a durable store.
// Gateway owns one instance, retires it on disconnect, and stops it before drain.
class PrivateReplayAdmission final
    : public std::enable_shared_from_this<PrivateReplayAdmission> {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Now = std::function<TimePoint()>;

    struct Token {
        std::uint64_t user_id{0};
        std::uint64_t epoch{0};
        std::uint64_t generation{0};
        std::uint64_t attempt{0};
    };
    struct Stats {
        std::size_t retained{0};
        std::size_t peak_retained{0};
        std::uint64_t ensured{0};
        std::uint64_t coalesced{0};
        std::uint64_t claims{0};
        std::uint64_t readmissions{0};
        std::uint64_t started{0};
        std::uint64_t deferred{0};
        std::uint64_t retired{0};
        bool stopped{false};
    };

    // Capture this lease in TaskSpec::work. If Submit rejects, or an accepted
    // task expires/is cancelled before invoking work, destruction re-arms the
    // exact intent. It does not need a Reactor completion to do so.
    // MarkStarted() must be called only from the real work body, NOT at Submit.
    class Attempt final {
    public:
        ~Attempt() noexcept;
        Attempt(const Attempt&) = delete;
        Attempt& operator=(const Attempt&) = delete;
        const Token& Identity() const noexcept { return token_; }
        bool MarkStarted();
    private:
        friend class PrivateReplayAdmission;
        Attempt(std::weak_ptr<PrivateReplayAdmission> owner, Token token)
            : owner_(std::move(owner)), token_(token) {}
        std::weak_ptr<PrivateReplayAdmission> owner_;
        Token token_;
        // Arm only after shared_ptr control-block allocation succeeds.
        std::atomic<bool> settled_{true};
    };
    using AttemptPtr = std::shared_ptr<Attempt>;

    explicit PrivateReplayAdmission(Now now = [] { return Clock::now(); });
    PrivateReplayAdmission(const PrivateReplayAdmission&) = delete;
    PrivateReplayAdmission& operator=(const PrivateReplayAdmission&) = delete;

    bool Ensure(std::uint64_t user_id, std::uint64_t epoch);
    AttemptPtr Claim(std::uint64_t user_id, std::uint64_t epoch);
    // Fair round-robin scan with explicit CPU/admission budgets per timer turn.
    std::vector<AttemptPtr> TakeDue(std::size_t scan_budget,
                                    std::size_t submit_budget);
    void Retire(std::uint64_t user_id, std::uint64_t epoch);
    void Stop();
    // Gateway may restart after Stop/drain; never reset generation identities.
    void Resume();
    Stats GetStats() const;

private:
    struct Entry {
        std::uint64_t epoch{0}, generation{0}, attempt{0};
        std::uint32_t deferrals{0};
        TimePoint due{};
        bool in_flight{false};
    };
    AttemptPtr ClaimLocked(std::uint64_t user_id, Entry& entry, TimePoint now);
    bool Started(const Token& token);
    void Abandoned(const Token& token);
    bool Matches(const Token& token, const Entry& entry) const noexcept;

    Now now_; // Production steady_clock; tests can supply a nonblocking fake clock.
    mutable std::mutex mutex_;
    std::map<std::uint64_t, Entry> entries_;
    std::uint64_t next_generation_{1};
    std::uint64_t scan_cursor_{0};
    Stats stats_;
};
} // namespace tinyimx
