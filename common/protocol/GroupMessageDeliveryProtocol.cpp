#include "common/protocol/GroupMessageDeliveryProtocol.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <utility>

namespace tinyimx {
namespace {
using Json = nlohmann::json;

void SetError(std::string* out, std::string value) {
    if (out != nullptr) *out = std::move(value);
}

bool ValidateDelivery(const GroupMessageDelivery& value, std::string* error) {
    if (value.message_id == 0) { SetError(error, "message_id is zero"); return false; }
    if (value.group_id == 0) { SetError(error, "group_id is zero"); return false; }
    if (value.from_user_id == 0) { SetError(error, "from_user_id is zero"); return false; }
    if (value.message_type == 0) { SetError(error, "message_type is zero"); return false; }
    if (value.content.empty()) { SetError(error, "content is empty"); return false; }
    return true;
}
}  // namespace

bool SerializeGroupMessageDelivery(
    const GroupMessageDelivery& delivery,
    std::string* output,
    std::string* error_message
) {
    if (output == nullptr) { SetError(error_message, "output is null"); return false; }
    if (!ValidateDelivery(delivery, error_message)) return false;
    Json body;
    body["message_id"] = delivery.message_id;
    body["group_id"] = delivery.group_id;
    body["from_user_id"] = delivery.from_user_id;
    body["message_type"] = delivery.message_type;
    body["content"] = delivery.content;
    body["created_at"] = delivery.created_at;
    *output = body.dump();
    if (error_message) error_message->clear();
    return true;
}

bool DeserializeGroupMessageDelivery(
    const std::string& input,
    GroupMessageDelivery* delivery,
    std::string* error_message
) {
    if (delivery == nullptr) { SetError(error_message, "delivery is null"); return false; }
    try {
        const Json body = Json::parse(input);
        if (!body.is_object()) { SetError(error_message, "delivery body is not object"); return false; }
        if (!body.contains("message_id") || !body["message_id"].is_number_unsigned() ||
            !body.contains("group_id") || !body["group_id"].is_number_unsigned() ||
            !body.contains("from_user_id") || !body["from_user_id"].is_number_unsigned() ||
            !body.contains("message_type") || !body["message_type"].is_number_unsigned() ||
            !body.contains("content") || !body["content"].is_string()) {
            SetError(error_message, "invalid group message delivery body");
            return false;
        }
        GroupMessageDelivery parsed;
        parsed.message_id = body["message_id"].get<std::uint64_t>();
        parsed.group_id = body["group_id"].get<std::uint64_t>();
        parsed.from_user_id = body["from_user_id"].get<std::uint64_t>();
        parsed.message_type = body["message_type"].get<std::uint32_t>();
        parsed.content = body["content"].get<std::string>();
        if (body.contains("created_at")) {
            if (!body["created_at"].is_string()) { SetError(error_message, "invalid created_at"); return false; }
            parsed.created_at = body["created_at"].get<std::string>();
        }
        if (!ValidateDelivery(parsed, error_message)) return false;
        *delivery = std::move(parsed);
        if (error_message) error_message->clear();
        return true;
    } catch (const std::exception& e) {
        SetError(error_message, std::string("invalid group delivery json: ") + e.what());
        return false;
    }
}

bool SerializeGroupMessageDeliveryAck(
    const GroupMessageDeliveryAck& ack,
    std::string* output,
    std::string* error_message
) {
    if (output == nullptr) { SetError(error_message, "output is null"); return false; }
    if (ack.message_id == 0) { SetError(error_message, "message_id is zero"); return false; }
    Json body;
    body["message_id"] = ack.message_id;
    *output = body.dump();
    if (error_message) error_message->clear();
    return true;
}

bool DeserializeGroupMessageDeliveryAck(
    const std::string& input,
    GroupMessageDeliveryAck* ack,
    std::string* error_message
) {
    if (ack == nullptr) { SetError(error_message, "ack is null"); return false; }
    try {
        const Json body = Json::parse(input);
        if (!body.is_object() || !body.contains("message_id") || !body["message_id"].is_number_unsigned()) {
            SetError(error_message, "invalid group delivery ack body");
            return false;
        }
        GroupMessageDeliveryAck parsed;
        parsed.message_id = body["message_id"].get<std::uint64_t>();
        if (parsed.message_id == 0) { SetError(error_message, "message_id is zero"); return false; }
        *ack = parsed;
        if (error_message) error_message->clear();
        return true;
    } catch (const std::exception& e) {
        SetError(error_message, std::string("invalid group delivery ack json: ") + e.what());
        return false;
    }
}

}  // namespace tinyimx
