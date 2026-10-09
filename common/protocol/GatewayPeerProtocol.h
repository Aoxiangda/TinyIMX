#pragma once

#include <cstdint>
#include <string>

namespace tinyimx {

/*
 * Gateway A -> Gateway B
 *
 * 转发一个已经由源 Gateway
 * 完成客户端身份确认的私聊消息。
 */
struct GatewayForwardChatRequest {
    /*
     * 业务消息唯一ID。
     *
     * 与 Packet::seq 不同：
     *
     * message_id:
     *   一条业务消息永久不变，
     *   跨连接、跨重试保持一致。
     *
     * Packet::seq:
     *   只负责一次 RPC 的
     *   Request / Response 关联。
     */
    std::uint64_t message_id{0};

    std::string source_gateway_id;

    /*
     * 源 Gateway 当前进程世代
     * 对应的 Registry Lease Token。
     */
    std::string source_lease_token;

    std::uint64_t from_user_id{0};

    std::uint64_t to_user_id{0};

    /*
     * 已经由源 Gateway 标准化后的
     * ChatMessage body。
     *
     * Gateway Peer 层只负责搬运，
     * 不理解具体聊天业务字段。
     */
    std::string message_body;
};


enum class GatewayForwardChatStatus {
    kDelivered = 0,

    kTargetNotLocal,

    kTargetNotConnected,

    /*
     * 同一个 message_id
     * 已经有另外一个请求正在处理。
     *
     * 当前请求不能再次执行本地 Push。
     */
    kDuplicateInProgress,

    kUnauthorized,

    kInvalidRequest,

    kInternalError
};


struct GatewayForwardChatResponse {
    GatewayForwardChatStatus status{
        GatewayForwardChatStatus::
            kInternalError
    };

    /*
     * 对应业务消息ID。
     *
     * 对于无法解析出 message_id 的
     * invalid_request，可以保持为 0。
     */
    std::uint64_t message_id{0};

    std::uint64_t to_user_id{0};

    std::string target_gateway_id;

    /*
     * false：
     *   本次真正执行了消息投递。
     *
     * true：
     *   message_id 以前已经处理完成，
     *   本次只是返回之前的处理结果，
     *   没有再次 Push。
     */
    bool duplicate{false};

    std::string error_message;


    bool Delivered() const noexcept {
        return status ==
            GatewayForwardChatStatus::
                kDelivered;
    }
};


std::string
GatewayForwardChatStatusToString(
    GatewayForwardChatStatus status
);


bool SerializeGatewayForwardChatRequest(
    const GatewayForwardChatRequest& request,
    std::string* output,
    std::string* error_message = nullptr
);


bool DeserializeGatewayForwardChatRequest(
    const std::string& input,
    GatewayForwardChatRequest* request,
    std::string* error_message = nullptr
);


bool SerializeGatewayForwardChatResponse(
    const GatewayForwardChatResponse& response,
    std::string* output,
    std::string* error_message = nullptr
);


bool DeserializeGatewayForwardChatResponse(
    const std::string& input,
    GatewayForwardChatResponse* response,
    std::string* error_message = nullptr
);

}  // namespace tinyimx