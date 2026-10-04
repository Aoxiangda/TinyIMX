#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace tinyimx {

// Bounded scheduling metadata only. Pending SQL rows remain the durable truth.
// No RPC, socket operation or Gateway pointer is retained under this mutex.
class DurablePrivateRecovery final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    struct Options {
        std::size_t max_session_states{65536};
        std::size_t max_active_replays{32};
        std::chrono::milliseconds query_interval{100};
        std::chrono::milliseconds failure_backoff{500};
    };
private:
    struct ReplayState {
        std::uint64_t epoch{0}, generation{0}, cursor{0};
        bool active{false};
    };
    struct State {
        explicit State(Options value) : options(value) {
            options.max_session_states = std::max<std::size_t>(1, options.max_session_states);
            options.max_active_replays = std::max<std::size_t>(1, options.max_active_replays);
        }
        Options options;
        std::mutex mutex;
        bool stopped{true}, query_active{false};
        std::uint64_t query_generation{0}, cursor{0}, replay_generation{0};
        TimePoint next_query{};
        std::deque<std::uint64_t> recipients;
        std::unordered_map<std::uint64_t, ReplayState> sessions;
        std::size_t active_replays{0};
    };
public:
    class QueryAttempt final {
    public:
        ~QueryAttempt() { Fail(); }
        std::uint64_t Cursor() const noexcept { return cursor_; }
        std::uint32_t Limit() const noexcept { return 256; }
        bool Complete(const std::vector<std::uint64_t>& ids, bool has_more,
                      TimePoint now = Clock::now()) {
            if (ids.size() > Limit() || (has_more && ids.size() != Limit())) return false;
            auto previous = cursor_;
            for (const auto id : ids) {
                if (id == 0 || id <= previous) return false;
                previous = id;
            }
            std::lock_guard lock(state_->mutex);
            if (!CurrentLocked()) return false;
            // Build first so allocation failure does not advance scan progress.
            std::deque<std::uint64_t> pending(ids.begin(), ids.end());
            state_->recipients.swap(pending);
            state_->cursor = has_more ? previous : 0;
            state_->next_query = now + state_->options.query_interval;
            state_->query_active = false;
            finished_ = true;
            return true;
        }
        void Fail(TimePoint now = Clock::now()) {
            std::lock_guard lock(state_->mutex);
            if (CurrentLocked()) {
                state_->query_active = false;
                state_->next_query = now + state_->options.failure_backoff;
            }
            finished_ = true;
        }
    private:
        friend class DurablePrivateRecovery;
    public:
        QueryAttempt(std::shared_ptr<State> state, std::uint64_t generation, std::uint64_t cursor)
            : state_(std::move(state)), generation_(generation), cursor_(cursor) {}
    private:
        bool CurrentLocked() const {
            return !finished_ && !state_->stopped && state_->query_active &&
                   state_->query_generation == generation_;
        }
        std::shared_ptr<State> state_;
        std::uint64_t generation_, cursor_;
        bool finished_{false};
    };
    using QueryAttemptPtr = std::shared_ptr<QueryAttempt>;

    class ReplayAttempt final {
    public:
        ~ReplayAttempt() { Release(); }
        std::uint64_t Cursor() const noexcept { return cursor_; }
        bool Complete(std::uint64_t last_message_id, bool has_more) {
            if (has_more && last_message_id <= cursor_) return false;
            std::lock_guard lock(state_->mutex);
            const auto it = state_->sessions.find(user_);
            if (!CurrentLocked(it)) return false;
            it->second.cursor = has_more ? last_message_id : 0;
            FinishLocked(it);
            return true;
        }
    private:
        friend class DurablePrivateRecovery;
        using Iterator = std::unordered_map<std::uint64_t, ReplayState>::iterator;
    public:
        ReplayAttempt(std::shared_ptr<State> state, std::uint64_t user, const ReplayState& replay)
            : state_(std::move(state)), user_(user), epoch_(replay.epoch),
              generation_(replay.generation), cursor_(replay.cursor) {}
    private:
        bool CurrentLocked(Iterator it) const {
            return !finished_ && !state_->stopped && it != state_->sessions.end() &&
                   it->second.epoch == epoch_ && it->second.generation == generation_ && it->second.active;
        }
        void FinishLocked(Iterator it) {
            it->second.active = false;
            --state_->active_replays;
            finished_ = true;
        }
        void Release() {
            std::lock_guard lock(state_->mutex);
            const auto it = state_->sessions.find(user_);
            if (CurrentLocked(it)) FinishLocked(it);
            finished_ = true;
        }
        std::shared_ptr<State> state_;
        std::uint64_t user_, epoch_, generation_, cursor_;
        bool finished_{false};
    };
    using ReplayAttemptPtr = std::shared_ptr<ReplayAttempt>;

    DurablePrivateRecovery() : DurablePrivateRecovery(Options{}) {}
    explicit DurablePrivateRecovery(Options options) : state_(std::make_shared<State>(options)) {}
    void Resume() {
        std::lock_guard lock(state_->mutex);
        state_->stopped = false;
        state_->query_active = false;
        ++state_->query_generation;
        state_->cursor = 0;
        state_->next_query = {};
        state_->recipients.clear();
        state_->sessions.clear();
        state_->active_replays = 0;
    }
    void Stop() {
        std::lock_guard lock(state_->mutex);
        state_->stopped = true;
        state_->query_active = false;
        ++state_->query_generation;
        state_->recipients.clear();
        state_->sessions.clear();
        state_->active_replays = 0;
    }
    bool Running() const {
        std::lock_guard lock(state_->mutex);
        return !state_->stopped;
    }
    QueryAttemptPtr AcquireQuery(TimePoint now = Clock::now()) {
        std::lock_guard lock(state_->mutex);
        if (state_->stopped || state_->query_active || !state_->recipients.empty() || now < state_->next_query)
            return {};
        const auto generation = state_->query_generation + 1;
        auto lease = std::make_shared<QueryAttempt>(state_, generation, state_->cursor);
        state_->query_generation = generation;
        state_->query_active = true;
        return lease;
    }
    std::vector<std::uint64_t> TakeRecipients(std::size_t budget = 32) {
        std::lock_guard lock(state_->mutex);
        std::vector<std::uint64_t> ids;
        if (state_->stopped) return ids;
        const auto count = std::min({budget, std::size_t{32}, state_->recipients.size()});
        ids.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            ids.push_back(state_->recipients.front());
            state_->recipients.pop_front();
        }
        return ids;
    }
    ReplayAttemptPtr BeginReplay(std::uint64_t user, std::uint64_t epoch) {
        if (user == 0 || epoch == 0) return {};
        std::lock_guard lock(state_->mutex);
        if (state_->stopped) return {};
        auto it = state_->sessions.find(user);
        if (it != state_->sessions.end() && it->second.epoch != epoch) {
            // SessionManager epochs increase. Old workers cannot move backwards.
            if (epoch < it->second.epoch) return {};
            if (it->second.active) --state_->active_replays;
            it->second = ReplayState{epoch, 0, 0, false};
        }
        if (it != state_->sessions.end() && it->second.active) return {};
        if (state_->active_replays >= state_->options.max_active_replays) return {};
        if (it == state_->sessions.end()) {
            if (state_->sessions.size() >= state_->options.max_session_states) return {};
            it = state_->sessions.emplace(user, ReplayState{epoch, 0, 0, false}).first;
        }
        auto& replay = it->second;
        replay.generation = ++state_->replay_generation;
        auto lease = std::make_shared<ReplayAttempt>(state_, user, replay);
        replay.active = true;
        ++state_->active_replays;
        return lease;
    }
    void Retire(std::uint64_t user, std::uint64_t epoch) {
        std::lock_guard lock(state_->mutex);
        const auto it = state_->sessions.find(user);
        if (it == state_->sessions.end() || it->second.epoch != epoch) return;
        if (it->second.active) --state_->active_replays;
        state_->sessions.erase(it);
    }
    std::size_t SessionStates() const {
        std::lock_guard lock(state_->mutex);
        return state_->sessions.size();
    }
private:
    std::shared_ptr<State> state_;
};

} // namespace tinyimx
