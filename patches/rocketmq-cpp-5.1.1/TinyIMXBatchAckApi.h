#pragma once
// Optional pinned-SDK GNU libstdc++ C++11 ABI extension. No protobuf types
// cross this boundary. Original SDK clients do not reference this symbol.
#include "rocketmq/Message.h"
#include "rocketmq/RocketMQ.h"
#include <cstdint>
#include <system_error>
#include <vector>
ROCKETMQ_NAMESPACE_BEGIN
class SimpleConsumer;
struct TinyIMXBatchAckStatsV1 {
    std::uint64_t api_calls;
    std::uint64_t rpc_submissions;
    std::uint64_t submitted_entries;
    std::uint64_t rpc_terminals;
};
struct TinyIMXBatchAckApiV1 {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t structure_bytes;
    std::uint32_t gnu_cxx11_abi;
    std::uint32_t maximum_entries;
    std::uint32_t consumer_bytes;
    std::uint32_t message_pointer_bytes;
    int (*ack_batch)(SimpleConsumer*,const std::vector<MessageConstSharedPtr>*,std::vector<std::error_code>*);
    void (*read_stats)(TinyIMXBatchAckStatsV1*);
};
constexpr std::uint32_t kTinyIMXBatchAckMagic=0x54494d58u;
extern "C" const TinyIMXBatchAckApiV1* tinyimx_rocketmq_batch_ack_api_v1() noexcept;
ROCKETMQ_NAMESPACE_END
