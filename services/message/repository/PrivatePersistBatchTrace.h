#pragma once

#include "services/message/repository/PrivatePersistenceTrace.h"

namespace tinyimx::message {
// Shared transaction costs stay labelled as batch costs, never divided into
// invented per-message commit durations or CPU charges across caller threads.
class PrivatePersistBatchTrace final {
public:
    enum Phase {Acquire, Begin, Lookup, Insert, Verify, Outbox, Commit, Recovery, Rollback, Direct, InsertVerify, Count};
    void SetGroupedQueryCommands(std::uint64_t count) noexcept {grouped_commands_=count;}
    static std::int64_t Now() noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    PrivatePersistBatchTrace() noexcept : enabled_(PrivatePersistenceTrace::EnvironmentEnabled()) {
        if (enabled_) {
            start_=Now();cpu_start_=diagnostics::ThreadCpuMicros();
            static std::atomic<std::uint64_t> next{0};id_=++next;
        }
    }
    template<class Fn> auto Measure(Phase phase, Fn&& fn)->std::invoke_result_t<Fn> {
        if (!enabled_) return std::forward<Fn>(fn)();
        const auto start=Now();
        try {
            auto result=std::forward<Fn>(fn)();time_[phase]+=Now()-start;return result;
        } catch (...) {time_[phase]+=Now()-start;throw;}
    }
    template<class Inputs, class Results>
    void Emit(const Inputs& inputs, const Results& results, std::size_t created) noexcept {
        if (!enabled_) return;
        const auto end=Now(),cpu=diagnostics::CpuDelta(cpu_start_,diagnostics::ThreadCpuMicros());
        static PersistenceLogLimiter limiters[2];
        try {
            for (std::size_t i=0;i<inputs.size()&&i<results.size();++i) {
                const auto& in=inputs[i];const auto& result=results[i];
                const bool slow=end-start_>=10000||!result.Completed();
                if (!slow&&!diagnostics::MessageTraceSelected(result.message_id)) continue;
                if (!limiters[slow?1:0].Admit(end/1000000)) continue;
                LOG_WARN("private_persist_batch from="<<in.from_user_id<<" to="<<in.to_user_id
                    <<" mid="<<result.message_id<<" batch_id="<<id_<<" batch_size="<<inputs.size()
                    <<" created="<<created<<" status="<<static_cast<int>(result.status)
                    <<" outcome="<<static_cast<int>(result.outcome)<<" batch_started_us="<<start_
                    <<" batch_total_us="<<(end-start_)<<" batch_cpu_us="<<cpu
                    <<" caller_queue_us="<<(in.queued_at_us?start_-in.queued_at_us:-1)
                    <<" caller_repo_us="<<(in.queued_at_us?end-in.queued_at_us:-1)
                    <<" acquire_us="<<time_[Acquire]<<" begin_us="<<time_[Begin]
                    <<" lookup_us="<<time_[Lookup]<<" insert_us="<<time_[Insert]
                    <<" verify_us="<<time_[Verify]<<" outbox_us="<<time_[Outbox]
                    <<" commit_us="<<time_[Commit]<<" recovery_us="<<time_[Recovery]
                    <<" rollback_us="<<time_[Rollback]<<" direct_us="<<time_[Direct]
                    <<" insert_verify_us="<<time_[InsertVerify]<<" grouped_query_commands="<<grouped_commands_);
            }
        } catch (...) { /* Observability cannot change a committed result. */ }
    }
private:
    bool enabled_{false};
    std::uint64_t id_{0};
    std::int64_t start_{0},cpu_start_{-1};
    std::array<std::int64_t,Count> time_{};
    std::uint64_t grouped_commands_{0};
};
} // namespace tinyimx::message
