#pragma once
// Optional pinned-SDK extension. No protobuf object crosses this ABI boundary.
#include "rocketmq/Message.h"
#include "rocketmq/RocketMQ.h"
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

ROCKETMQ_NAMESPACE_BEGIN
class Producer;
struct TinyIMXBatchSendStatsV1 {
    std::uint64_t api_calls;
    std::uint64_t rpc_submissions;
    std::uint64_t submitted_entries;
    std::uint64_t rpc_terminals;
    std::uint64_t malformed_responses;
    std::uint64_t synchronous_timeouts;
};
struct TinyIMXBatchSendApiV1 {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t structure_bytes;
    std::uint32_t gnu_cxx11_abi;
    std::uint32_t maximum_entries;
    std::uint32_t maximum_body_bytes;
    std::uint32_t producer_bytes;
    std::uint32_t message_pointer_bytes;
    int (*send_batch)(Producer*,const std::vector<MessageConstSharedPtr>*,
                      std::vector<std::error_code>*,std::vector<std::string>*);
    void (*read_stats)(TinyIMXBatchSendStatsV1*);
};
constexpr std::uint32_t kTinyIMXBatchSendMagic=0x54494d53u;
extern "C" const TinyIMXBatchSendApiV1* tinyimx_rocketmq_batch_send_api_v1() noexcept;
ROCKETMQ_NAMESPACE_END
