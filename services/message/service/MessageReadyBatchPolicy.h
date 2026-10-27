#pragma once
#include <cstdlib>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace tinyimx::message {
// Experiment only. Keep the existing sender shard, capacity and worker count.
// Limit1 isolates per-request SQL preparation from a batch of unrelated actors.
inline std::size_t ParsePrivateReadyBatchLimit(const char* value) {
    if(value==nullptr||std::string_view(value)=="64")return 64;
    if(std::string_view(value)=="1")return 1;
    throw std::invalid_argument("TINYIMX_PRIVATE_READY_BATCH_LIMIT supports only1 or64");
}
inline std::size_t ConfiguredPrivateReadyBatchLimit() {
    return ParsePrivateReadyBatchLimit(std::getenv("TINYIMX_PRIVATE_READY_BATCH_LIMIT"));
}
inline bool ParsePrivateReadyKeyedScheduler(const char* value) {
    if(value==nullptr||std::string_view(value)=="0")return false;
    if(std::string_view(value)=="1")return true;
    throw std::invalid_argument("TINYIMX_PRIVATE_READY_KEYED_SCHEDULER_ENABLE supports only0 or1");
}
inline bool ConfiguredPrivateReadyKeyedScheduler() {
    return ParsePrivateReadyKeyedScheduler(std::getenv("TINYIMX_PRIVATE_READY_KEYED_SCHEDULER_ENABLE"));
}
}
