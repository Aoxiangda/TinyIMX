#pragma once
#include "rocketmq/TinyIMXBatchAckApi.h"
#include <atomic>
namespace tinyimx_mq_ack_metrics {
struct Counters {
    std::atomic<std::uint64_t> calls{0},submissions{0},entries{0},terminals{0};
};
inline Counters& Get(){static Counters c;return c;}
inline void Read(ROCKETMQ_NAMESPACE::TinyIMXBatchAckStatsV1*out) noexcept {
    if(!out)return;
    auto& c=Get();
    out->api_calls=c.calls.load(std::memory_order_relaxed);
    out->rpc_submissions=c.submissions.load(std::memory_order_relaxed);
    out->submitted_entries=c.entries.load(std::memory_order_relaxed);
    out->rpc_terminals=c.terminals.load(std::memory_order_relaxed);
}
}
