#include "gateway/PrivateReplayAdmission.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tinyimx {
PrivateReplayAdmission::PrivateReplayAdmission(Now now) : now_(std::move(now)) {
    if (!now_) throw std::invalid_argument("private replay clock is required");
}
PrivateReplayAdmission::Attempt::~Attempt() noexcept {
    if (!settled_.exchange(true, std::memory_order_acq_rel)) {
        if (auto owner = owner_.lock()) owner->Abandoned(token_);
    }
}
bool PrivateReplayAdmission::Attempt::MarkStarted() {
    if (settled_.exchange(true, std::memory_order_acq_rel)) return false;
    if (auto owner = owner_.lock()) return owner->Started(token_);
    return false;
}
bool PrivateReplayAdmission::Ensure(std::uint64_t user_id, std::uint64_t epoch) {
    if (user_id == 0 || epoch == 0) return false;
    const auto now = now_();
    std::lock_guard lock(mutex_);
    if (stats_.stopped) return false;
    auto it = entries_.find(user_id);
    if (it != entries_.end()) {
        if (it->second.epoch >= epoch) {
            ++stats_.coalesced;
            return false;
        }
        ++stats_.retired; // An old task can no longer settle the new generation.
    }
    Entry e;
    e.epoch = epoch;
    e.generation = next_generation_++;
    e.due = now;
    entries_.insert_or_assign(user_id, e);
    ++stats_.ensured;
    stats_.peak_retained = std::max(stats_.peak_retained, entries_.size());
    return true;
}
PrivateReplayAdmission::AttemptPtr PrivateReplayAdmission::ClaimLocked(
    std::uint64_t user_id, Entry& e, TimePoint now) {
    if (e.in_flight || e.due > now) return {};
    auto owner = weak_from_this();
    if (owner.expired()) throw std::logic_error("replay admission must be shared-owned");
    Token token{user_id, e.epoch, e.generation, e.attempt + 1};
    AttemptPtr lease(new Attempt(owner, token));
    e.attempt = token.attempt;
    e.in_flight = true;
    lease->settled_.store(false, std::memory_order_release);
    ++stats_.claims;
    if (e.attempt > 1) ++stats_.readmissions;
    return lease;
}
PrivateReplayAdmission::AttemptPtr PrivateReplayAdmission::Claim(
    std::uint64_t user_id, std::uint64_t epoch) {
    const auto now = now_();
    std::lock_guard lock(mutex_);
    if (stats_.stopped) return {};
    auto it = entries_.find(user_id);
    if (it == entries_.end() || it->second.epoch != epoch) return {};
    return ClaimLocked(user_id, it->second, now);
}
std::vector<PrivateReplayAdmission::AttemptPtr> PrivateReplayAdmission::TakeDue(
    std::size_t scan_budget, std::size_t submit_budget) {
    // Reserve before taking mutex: no lease destruction/re-entry on allocation.
    std::vector<AttemptPtr> result;
    result.reserve(submit_budget);
    const auto now = now_();
    std::lock_guard lock(mutex_);
    if (stats_.stopped || entries_.empty() || submit_budget == 0) return result;
    const auto scans = std::min(scan_budget, entries_.size());
    auto it = entries_.upper_bound(scan_cursor_);
    for (std::size_t n = 0; n < scans && result.size() < submit_budget; ++n) {
        if (it == entries_.end()) it = entries_.begin();
        scan_cursor_ = it->first;
        if (auto claim = ClaimLocked(it->first, it->second, now))
            result.push_back(std::move(claim));
        ++it;
    }
    return result;
}
bool PrivateReplayAdmission::Matches(const Token& t, const Entry& e) const noexcept {
    return t.epoch == e.epoch && t.generation == e.generation &&
           t.attempt == e.attempt && e.in_flight;
}
bool PrivateReplayAdmission::Started(const Token& token) {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(token.user_id);
    if (stats_.stopped || it == entries_.end() || !Matches(token, it->second)) return false;
    entries_.erase(it);
    ++stats_.started;
    return true;
}
void PrivateReplayAdmission::Abandoned(const Token& token) {
    const auto now = now_();
    std::lock_guard lock(mutex_);
    auto it = entries_.find(token.user_id);
    if (stats_.stopped || it == entries_.end() || !Matches(token, it->second)) return;
    auto& e = it->second;
    // Capped exponential delay + deterministic per-user/generation/attempt jitter.
    // No retry-count cutoff: a live session must not lose admission responsibility.
    const auto shift = std::min<std::uint32_t>(e.deferrals, 3);
    const auto delay = std::chrono::milliseconds{250 * (1 << shift)};
    const auto mix = token.user_id * 11400714819323198485ull ^
                     token.generation * 6364136223846793005ull ^ token.attempt;
    e.due = now + delay + std::chrono::milliseconds{mix % 126};
    e.in_flight = false;
    if (e.deferrals < 3) ++e.deferrals;
    ++stats_.deferred;
}
void PrivateReplayAdmission::Retire(std::uint64_t user_id, std::uint64_t epoch) {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(user_id);
    if (it != entries_.end() && it->second.epoch == epoch) {
        entries_.erase(it);
        ++stats_.retired;
    }
}
void PrivateReplayAdmission::Stop() {
    std::lock_guard lock(mutex_);
    stats_.stopped = true;
    stats_.retired += entries_.size();
    entries_.clear();
}
void PrivateReplayAdmission::Resume() {
    std::lock_guard lock(mutex_);
    stats_.stopped = false;
}
PrivateReplayAdmission::Stats PrivateReplayAdmission::GetStats() const {
    std::lock_guard lock(mutex_);
    auto snapshot = stats_;
    snapshot.retained = entries_.size();
    return snapshot;
}
} // namespace tinyimx
