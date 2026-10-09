#pragma once

#include <cstdint>
#include <string>

namespace tinyimx {

struct GroupMessageDelivery {
    std::uint64_t message_id{0};
    std::uint64_t group_id{0};
    std::uint64_t from_user_id{0};
    std::uint32_t message_type{0};
    std::string content;
    std::string created_at;
};

struct GroupMessageDeliveryAck {
    std::uint64_t message_id{0};
};

bool SerializeGroupMessageDelivery(
    const GroupMessageDelivery& delivery,
    std::string* output,
    std::string* error_message = nullptr
);

bool DeserializeGroupMessageDelivery(
    const std::string& input,
    GroupMessageDelivery* delivery,
    std::string* error_message = nullptr
);

bool SerializeGroupMessageDeliveryAck(
    const GroupMessageDeliveryAck& ack,
    std::string* output,
    std::string* error_message = nullptr
);

bool DeserializeGroupMessageDeliveryAck(
    const std::string& input,
    GroupMessageDeliveryAck* ack,
    std::string* error_message = nullptr
);

}  // namespace tinyimx
