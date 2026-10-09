#include "common/protocol/ClientChatProtocol.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <limits>
#include <utility>

namespace {

using Json = nlohmann::json;


void SetError(
    std::string* error_message,
    std::string message
) {
    if (error_message != nullptr) {
        *error_message =
            std::move(message);
    }
}


bool ReadPositiveUserId(
    const Json& object,
    const char* field_name,
    std::uint64_t* value,
    std::string* error_message
) {
    if (
        field_name == nullptr ||
        value == nullptr
    ) {
        SetError(
            error_message,
            "invalid user id output"
        );

        return false;
    }


    if (!object.contains(field_name)) {
        SetError(
            error_message,
            std::string("missing field: ") +
                field_name
        );

        return false;
    }


    const Json& field =
        object.at(field_name);


    if (field.is_number_unsigned()) {
        const auto parsed =
            field.get<std::uint64_t>();

        if (parsed == 0) {
            SetError(
                error_message,
                std::string(field_name) +
                    " must be positive"
            );

            return false;
        }


        *value = parsed;

        return true;
    }


    if (field.is_number_integer()) {
        const auto parsed =
            field.get<std::int64_t>();

        if (parsed <= 0) {
            SetError(
                error_message,
                std::string(field_name) +
                    " must be positive"
            );

            return false;
        }


        *value =
            static_cast<std::uint64_t>(
                parsed
            );

        return true;
    }


    SetError(
        error_message,
        std::string(field_name) +
            " must be an integer"
    );

    return false;
}


bool ReadPositiveMessageId(
    const Json& object,
    std::uint64_t* value,
    std::string* error_message
) {
    if (value == nullptr) {
        SetError(
            error_message,
            "invalid message_id output"
        );

        return false;
    }


    if (!object.contains("message_id")) {
        SetError(
            error_message,
            "missing field: message_id"
        );

        return false;
    }


    const Json& field =
        object.at("message_id");


    if (field.is_number_unsigned()) {
        const auto parsed =
            field.get<std::uint64_t>();

        if (parsed == 0) {
            SetError(
                error_message,
                "message_id must be positive"
            );

            return false;
        }


        *value =
            parsed;

        return true;
    }


    if (field.is_number_integer()) {
        const auto parsed =
            field.get<std::int64_t>();

        if (parsed <= 0) {
            SetError(
                error_message,
                "message_id must be positive"
            );

            return false;
        }


        *value =
            static_cast<std::uint64_t>(
                parsed
            );

        return true;
    }


    SetError(
        error_message,
        "message_id must be an integer"
    );

    return false;
}


bool ValidateClientMessageId(
    const std::string& value,
    std::string* error_message
) {
    if (value.empty()) {
        SetError(
            error_message,
            "client_message_id must not be empty"
        );

        return false;
    }


    if (
        value.size() >
        tinyimx::kMaxClientMessageIdSize
    ) {
        SetError(
            error_message,
            "client_message_id is too long"
        );

        return false;
    }


    return true;
}

}  // namespace


namespace tinyimx {

bool SerializeClientChatRequest(
    const ClientChatRequest& request,
    std::string* body,
    std::string* error_message
) {
    if (body == nullptr) {
        SetError(
            error_message,
            "output body is null"
        );

        return false;
    }


    if (
        !ValidateClientMessageId(
            request.client_message_id,
            error_message
        )
    ) {
        return false;
    }


    if (request.to_user_id == 0) {
        SetError(
            error_message,
            "to must be positive"
        );

        return false;
    }


    if (request.text.empty()) {
        SetError(
            error_message,
            "text must not be empty"
        );

        return false;
    }


    Json json{
        {
            "client_message_id",
            request.client_message_id
        },
        {
            "to",
            request.to_user_id
        },
        {
            "text",
            request.text
        }
    };


    if (
        request.claimed_from_user_id.
            has_value()
    ) {
        if (
            request.claimed_from_user_id.
                value() == 0
        ) {
            SetError(
                error_message,
                "from must be positive"
            );

            return false;
        }


        json["from"] =
            request.claimed_from_user_id.
                value();
    }


    *body = json.dump();

    return true;
}


bool DeserializeClientChatRequest(
    const std::string& body,
    ClientChatRequest* request,
    std::string* error_message
) {
    if (request == nullptr) {
        SetError(
            error_message,
            "request output is null"
        );

        return false;
    }


    Json json;


    try {
        json = Json::parse(body);
    } catch (const std::exception& e) {
        SetError(
            error_message,
            std::string("invalid chat json: ") +
                e.what()
        );

        return false;
    }


    if (!json.is_object()) {
        SetError(
            error_message,
            "chat body must be an object"
        );

        return false;
    }


    if (
        !json.contains("client_message_id") ||
        !json.at("client_message_id").
            is_string()
    ) {
        SetError(
            error_message,
            "missing or invalid client_message_id"
        );

        return false;
    }


    const std::string client_message_id =
        json.at("client_message_id").
            get<std::string>();


    if (
        !ValidateClientMessageId(
            client_message_id,
            error_message
        )
    ) {
        return false;
    }


    std::uint64_t to_user_id = 0;


    if (
        !ReadPositiveUserId(
            json,
            "to",
            &to_user_id,
            error_message
        )
    ) {
        return false;
    }


    if (
        !json.contains("text") ||
        !json.at("text").is_string()
    ) {
        SetError(
            error_message,
            "missing or invalid text"
        );

        return false;
    }


    const std::string text =
        json.at("text").
            get<std::string>();


    if (text.empty()) {
        SetError(
            error_message,
            "text must not be empty"
        );

        return false;
    }


    std::optional<std::uint64_t>
        claimed_from_user_id;


    if (json.contains("from")) {
        std::uint64_t from_user_id = 0;


        if (
            !ReadPositiveUserId(
                json,
                "from",
                &from_user_id,
                error_message
            )
        ) {
            return false;
        }


        claimed_from_user_id =
            from_user_id;
    }


    ClientChatRequest parsed;

    parsed.client_message_id =
        client_message_id;

    parsed.to_user_id =
        to_user_id;

    parsed.text =
        text;

    parsed.claimed_from_user_id =
        claimed_from_user_id;


    *request =
        std::move(parsed);

    return true;
}


bool SerializeClientChatAck(
    const ClientChatAck& ack,
    std::string* body,
    std::string* error_message
) {
    if (body == nullptr) {
        SetError(
            error_message,
            "output body is null"
        );

        return false;
    }


    /*
     * 成功ACK必须完整标识：
     *
     * Client业务请求
     * +
     * Server业务消息
     */
    if (ack.success) {
        if (
            !ValidateClientMessageId(
                ack.client_message_id,
                error_message
            )
        ) {
            return false;
        }


        if (ack.message_id == 0) {
            SetError(
                error_message,
                "successful chat ack requires message_id"
            );

            return false;
        }


        if (
            ack.from_user_id == 0 ||
            ack.to_user_id == 0
        ) {
            SetError(
                error_message,
                "successful chat ack requires valid users"
            );

            return false;
        }
    } else {
        /*
         * 失败ACK可能发生在client_message_id
         * 本身都没有成功解析之前。
         *
         * 所以允许为空。
         */
        if (
            !ack.client_message_id.empty() &&
            !ValidateClientMessageId(
                ack.client_message_id,
                error_message
            )
        ) {
            return false;
        }
    }


    Json json{
        {"success", ack.success},
        {"delivered", ack.delivered},
        {
            "stored_offline",
            ack.stored_offline
        },
        {
            "stored_persistent",
            ack.stored_persistent
        },
        {"reused", ack.reused},
        {
            "client_message_id",
            ack.client_message_id
        },
        {"message_id", ack.message_id},
        {"from", ack.from_user_id},
        {"to", ack.to_user_id},
        {
            "receiver_private_unread",
            ack.receiver_private_unread
        },
        {
            "receiver_total_unread",
            ack.receiver_total_unread
        },
        {"reason", ack.reason}
    };


    if (!ack.remote_gateway_id.empty()) {
        json["remote_gateway_id"] =
            ack.remote_gateway_id;
    }


    if (!ack.remote_host.empty()) {
        json["remote_host"] =
            ack.remote_host;
    }


    if (ack.remote_port != 0) {
        json["remote_port"] =
            ack.remote_port;
    }


    *body = json.dump();

    return true;
}


bool DeserializeClientChatAck(
    const std::string& body,
    ClientChatAck* ack,
    std::string* error_message
) {
    if (ack == nullptr) {
        SetError(
            error_message,
            "ack output is null"
        );

        return false;
    }


    Json json;


    try {
        json = Json::parse(body);
    } catch (const std::exception& e) {
        SetError(
            error_message,
            std::string("invalid chat ack json: ") +
                e.what()
        );

        return false;
    }


    if (!json.is_object()) {
        SetError(
            error_message,
            "chat ack body must be an object"
        );

        return false;
    }


    if (
        !json.contains("success") ||
        !json.at("success").is_boolean()
    ) {
        SetError(
            error_message,
            "missing or invalid success"
        );

        return false;
    }


    ClientChatAck parsed;

    parsed.success =
        json.at("success").get<bool>();

    parsed.delivered =
        json.value("delivered", false);

    parsed.stored_offline =
        json.value(
            "stored_offline",
            false
        );

    parsed.stored_persistent =
        json.value(
            "stored_persistent",
            false
        );

    parsed.reused =
        json.value("reused", false);


    if (
        json.contains("client_message_id")
    ) {
        if (
            !json.at("client_message_id").
                is_string()
        ) {
            SetError(
                error_message,
                "invalid client_message_id"
            );

            return false;
        }


        parsed.client_message_id =
            json.at("client_message_id").
                get<std::string>();
    }


    parsed.message_id =
        json.value(
            "message_id",
            std::uint64_t{0}
        );


    parsed.from_user_id =
        json.value(
            "from",
            std::uint64_t{0}
        );


    parsed.to_user_id =
        json.value(
            "to",
            std::uint64_t{0}
        );


    parsed.receiver_private_unread =
        json.value(
            "receiver_private_unread",
            std::int64_t{0}
        );


    parsed.receiver_total_unread =
        json.value(
            "receiver_total_unread",
            std::int64_t{0}
        );


    parsed.reason =
        json.value(
            "reason",
            std::string{}
        );


    parsed.remote_gateway_id =
        json.value(
            "remote_gateway_id",
            std::string{}
        );


    parsed.remote_host =
        json.value(
            "remote_host",
            std::string{}
        );


    parsed.remote_port =
        json.value(
            "remote_port",
            std::uint16_t{0}
        );


    if (parsed.success) {
        if (
            !ValidateClientMessageId(
                parsed.client_message_id,
                error_message
            )
        ) {
            return false;
        }


        if (parsed.message_id == 0) {
            SetError(
                error_message,
                "successful chat ack requires message_id"
            );

            return false;
        }


        if (
            parsed.from_user_id == 0 ||
            parsed.to_user_id == 0
        ) {
            SetError(
                error_message,
                "successful chat ack requires valid users"
            );

            return false;
        }
    }


    *ack =
        std::move(parsed);

    return true;
}

bool SerializeServerChatDelivery(
    const ServerChatDelivery& delivery,
    std::string* body,
    std::string* error_message
) {
    if (body == nullptr) {
        SetError(
            error_message,
            "output body is null"
        );

        return false;
    }


    if (delivery.message_id == 0) {
        SetError(
            error_message,
            "message_id must be positive"
        );

        return false;
    }


    if (
        delivery.from_user_id == 0 ||
        delivery.to_user_id == 0
    ) {
        SetError(
            error_message,
            "chat delivery requires valid users"
        );

        return false;
    }


    /*
     * 当前Client Chat协议真实语义
     * 只要求text非空。
     *
     * 本阶段不额外发明新的text长度限制。
     */
    if (delivery.text.empty()) {
        SetError(
            error_message,
            "text must not be empty"
        );

        return false;
    }


    Json json{
        {
            "message_id",
            delivery.message_id
        },
        {
            "from",
            delivery.from_user_id
        },
        {
            "to",
            delivery.to_user_id
        },
        {
            "text",
            delivery.text
        }
    };


    *body =
        json.dump();

    return true;
}


bool DeserializeServerChatDelivery(
    const std::string& body,
    ServerChatDelivery* delivery,
    std::string* error_message
) {
    if (delivery == nullptr) {
        SetError(
            error_message,
            "delivery output is null"
        );

        return false;
    }


    Json json;


    try {
        json =
            Json::parse(body);
    } catch (const std::exception& e) {
        SetError(
            error_message,
            std::string(
                "invalid chat delivery json: "
            ) +
                e.what()
        );

        return false;
    }


    if (!json.is_object()) {
        SetError(
            error_message,
            "chat delivery body must be an object"
        );

        return false;
    }


    std::uint64_t message_id =
        0;


    if (
        !ReadPositiveMessageId(
            json,
            &message_id,
            error_message
        )
    ) {
        return false;
    }


    std::uint64_t from_user_id =
        0;


    if (
        !ReadPositiveUserId(
            json,
            "from",
            &from_user_id,
            error_message
        )
    ) {
        return false;
    }


    std::uint64_t to_user_id =
        0;


    if (
        !ReadPositiveUserId(
            json,
            "to",
            &to_user_id,
            error_message
        )
    ) {
        return false;
    }


    if (
        !json.contains("text") ||
        !json.at("text").is_string()
    ) {
        SetError(
            error_message,
            "missing or invalid text"
        );

        return false;
    }


    const std::string text =
        json.at("text").
            get<std::string>();


    if (text.empty()) {
        SetError(
            error_message,
            "text must not be empty"
        );

        return false;
    }


    ServerChatDelivery parsed;

    parsed.message_id =
        message_id;

    parsed.from_user_id =
        from_user_id;

    parsed.to_user_id =
        to_user_id;

    parsed.text =
        text;


    *delivery =
        std::move(parsed);

    return true;
}


bool SerializeReceiverChatDeliveryAck(
    const ReceiverChatDeliveryAck& ack,
    std::string* body,
    std::string* error_message
) {
    if (body == nullptr) {
        SetError(
            error_message,
            "output body is null"
        );

        return false;
    }


    if (ack.message_id == 0) {
        SetError(
            error_message,
            "message_id must be positive"
        );

        return false;
    }


    Json json{
        {
            "message_id",
            ack.message_id
        }
    };


    *body =
        json.dump();

    return true;
}


bool DeserializeReceiverChatDeliveryAck(
    const std::string& body,
    ReceiverChatDeliveryAck* ack,
    std::string* error_message
) {
    if (ack == nullptr) {
        SetError(
            error_message,
            "delivery ack output is null"
        );

        return false;
    }


    Json json;


    try {
        json =
            Json::parse(body);
    } catch (const std::exception& e) {
        SetError(
            error_message,
            std::string(
                "invalid chat delivery ack json: "
            ) +
                e.what()
        );

        return false;
    }


    if (!json.is_object()) {
        SetError(
            error_message,
            "chat delivery ack body must be an object"
        );

        return false;
    }


    std::uint64_t message_id =
        0;


    if (
        !ReadPositiveMessageId(
            json,
            &message_id,
            error_message
        )
    ) {
        return false;
    }


    ReceiverChatDeliveryAck parsed;

    parsed.message_id =
        message_id;


    *ack =
        std::move(parsed);

    return true;
}


}  // namespace tinyimx