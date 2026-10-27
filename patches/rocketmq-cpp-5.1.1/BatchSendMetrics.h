#pragma once
#include "rocketmq/TinyIMXBatchSendApi.h"
#include <atomic>
namespace tinyimx_mq_send_metrics {
struct Counters {
    std::atomic<std::uint64_t> calls{0},submissions{0},entries{0},terminals{0};
    std::atomic<std::uint64_t> malformed{0},timeouts{0};
};
inline Counters& Get(){static Counters counters;return counters;}
inline void Read(ROCKETMQ_NAMESPACE::TinyIMXBatchSendStatsV1* out) noexcept {
    if(!out)return;
    auto& c=Get();
    out->api_calls=c.calls.load(std::memory_order_relaxed);
    out->rpc_submissions=c.submissions.load(std::memory_order_relaxed);
    out->submitted_entries=c.entries.load(std::memory_order_relaxed);
    out->rpc_terminals=c.terminals.load(std::memory_order_relaxed);
    out->malformed_responses=c.malformed.load(std::memory_order_relaxed);
    out->synchronous_timeouts=c.timeouts.load(std::memory_order_relaxed);
}
}
