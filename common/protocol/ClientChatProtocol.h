#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx {

/*
 * 与Live DB：
 *
 * client_message_id VARCHAR(64)
 *
 * 保持一致。
 */
constexpr std::size_t
    kMaxClientMessageIdSize = 64;


/*
 * Client -> Gateway
 *
 * 一次“发送消息”业务操作的协议模型。
 *
 * Packet.seq不属于这里：
 * seq属于网络Attempt，
 * 已经存在Packet Header里。
 */
struct ClientChatRequest {
    std::string client_message_id;

    std::uint64_t to_user_id{0};

    std::string text;


    /*
     * 兼容目前部分Demo仍发送：
     *
     * "from": 10001
     *
     * 它不是可信身份。
     * 下一阶段Gateway仍必须使用Session身份进行校验。
     *
     * 新Client可以完全不发送from。
     */
    std::optional<std::uint64_t>
        claimed_from_user_id;
};


/*
 * Gateway -> Client
 *
 * message_id：
 * 服务端持久化消息ID。
 *
 * client_message_id：
 * Client业务幂等ID。
 *
 * reused：
 * true表示当前ACK来自同一Client业务请求的重试，
 * 服务端没有创建第二条消息。
 */
struct ClientChatAck {
    bool success{false};

    bool delivered{false};

    bool stored_offline{false};

    bool stored_persistent{false};

    bool reused{false};


    std::string client_message_id;

    std::uint64_t message_id{0};

    std::uint64_t from_user_id{0};

    std::uint64_t to_user_id{0};


    std::int64_t receiver_private_unread{0};

    std::int64_t receiver_total_unread{0};


    std::string reason;


    /*
     * 跨Gateway成功/失败时的辅助诊断信息。
     *
     * Local Chat可以为空。
     */
    std::string remote_gateway_id;

    std::string remote_host;

    std::uint16_t remote_port{0};
};


/*
 * Gateway -> Receiver Client
 *
 * 一次Server Message的Receiver投递协议模型。
 *
 * Packet.seq不放在body中。
 *
 * Packet.seq：
 *     当前一次Delivery Attempt Identity。
 *
 * message_id：
 *     稳定Server Business Message Identity。
 *
 * 同一条message_id允许经历多个不同Packet.seq。
 */
struct ServerChatDelivery {
    std::uint64_t message_id{0};

    std::uint64_t from_user_id{0};

    std::uint64_t to_user_id{0};

    std::string text;
};


/*
 * Receiver Client -> Gateway
 *
 * Receiver应用协议层已经成功解析并接受：
 *
 *     server message_id
 *
 * Packet.seq仍然在Packet Header中，
 * 用于标识ACK对应的Delivery Attempt。
 *
 * Receiver身份不放在body中：
 * Gateway必须从Authenticated Session取得真实用户身份。
 */
struct ReceiverChatDeliveryAck {
    std::uint64_t message_id{0};
};


bool SerializeClientChatRequest(
    const ClientChatRequest& request,
    std::string* body,
    std::string* error_message = nullptr
);


bool DeserializeClientChatRequest(
    const std::string& body,
    ClientChatRequest* request,
    std::string* error_message = nullptr
);


bool SerializeClientChatAck(
    const ClientChatAck& ack,
    std::string* body,
    std::string* error_message = nullptr
);


bool DeserializeClientChatAck(
    const std::string& body,
    ClientChatAck* ack,
    std::string* error_message = nullptr
);


bool SerializeServerChatDelivery(
    const ServerChatDelivery& delivery,
    std::string* body,
    std::string* error_message = nullptr
);


bool DeserializeServerChatDelivery(
    const std::string& body,
    ServerChatDelivery* delivery,
    std::string* error_message = nullptr
);


bool SerializeReceiverChatDeliveryAck(
    const ReceiverChatDeliveryAck& ack,
    std::string* body,
    std::string* error_message = nullptr
);


bool DeserializeReceiverChatDeliveryAck(
    const std::string& body,
    ReceiverChatDeliveryAck* ack,
    std::string* error_message = nullptr
);


}  // namespace tinyimx