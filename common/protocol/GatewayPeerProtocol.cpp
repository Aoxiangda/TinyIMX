#include "common/protocol/GatewayPeerProtocol.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <utility>

namespace tinyimx {
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


bool ValidateRequest(
    const GatewayForwardChatRequest&
        request,
    std::string* error_message
) {
    /*
     * message_id 是现在这一阶段
     * 最重要的新约束。
     */
    if (request.message_id == 0) {
        SetError(
            error_message,
            "message_id is zero"
        );

        return false;
    }


    if (
        request.source_gateway_id.empty()
    ) {
        SetError(
            error_message,
            "source_gateway_id is empty"
        );

        return false;
    }


    if (
        request.source_lease_token.empty()
    ) {
        SetError(
            error_message,
            "source_lease_token is empty"
        );

        return false;
    }


    if (request.from_user_id == 0) {
        SetError(
            error_message,
            "from_user_id is zero"
        );

        return false;
    }


    if (request.to_user_id == 0) {
        SetError(
            error_message,
            "to_user_id is zero"
        );

        return false;
    }


    if (
        request.from_user_id ==
        request.to_user_id
    ) {
        SetError(
            error_message,
            "from_user_id equals to_user_id"
        );

        return false;
    }


    if (request.message_body.empty()) {
        SetError(
            error_message,
            "message_body is empty"
        );

        return false;
    }


    return true;
}


bool ResponseRequiresTargetUser(
    GatewayForwardChatStatus status
) {
    switch (status) {
        case GatewayForwardChatStatus::
            kDelivered:

        case GatewayForwardChatStatus::
            kTargetNotLocal:

        case GatewayForwardChatStatus::
            kTargetNotConnected:

        case GatewayForwardChatStatus::
            kDuplicateInProgress:
            return true;


        case GatewayForwardChatStatus::
            kUnauthorized:

        case GatewayForwardChatStatus::
            kInvalidRequest:

        case GatewayForwardChatStatus::
            kInternalError:
            return false;
    }


    return false;
}


bool ResponseRequiresMessageId(
    GatewayForwardChatStatus status
) {
    switch (status) {
        case GatewayForwardChatStatus::
            kDelivered:

        case GatewayForwardChatStatus::
            kTargetNotLocal:

        case GatewayForwardChatStatus::
            kTargetNotConnected:

        case GatewayForwardChatStatus::
            kDuplicateInProgress:
            return true;


        case GatewayForwardChatStatus::
            kUnauthorized:

        case GatewayForwardChatStatus::
            kInvalidRequest:

        case GatewayForwardChatStatus::
            kInternalError:
            return false;
    }


    return false;
}


bool ParseStatus(
    const std::string& value,
    GatewayForwardChatStatus* status
) {
    if (status == nullptr) {
        return false;
    }


    if (value == "delivered") {
        *status =
            GatewayForwardChatStatus::
                kDelivered;

        return true;
    }


    if (value == "target_not_local") {
        *status =
            GatewayForwardChatStatus::
                kTargetNotLocal;

        return true;
    }


    if (
        value ==
        "target_not_connected"
    ) {
        *status =
            GatewayForwardChatStatus::
                kTargetNotConnected;

        return true;
    }


    if (
        value ==
        "duplicate_in_progress"
    ) {
        *status =
            GatewayForwardChatStatus::
                kDuplicateInProgress;

        return true;
    }


    if (value == "unauthorized") {
        *status =
            GatewayForwardChatStatus::
                kUnauthorized;

        return true;
    }


    if (value == "invalid_request") {
        *status =
            GatewayForwardChatStatus::
                kInvalidRequest;

        return true;
    }


    if (value == "internal_error") {
        *status =
            GatewayForwardChatStatus::
                kInternalError;

        return true;
    }


    return false;
}

}  // namespace


std::string
GatewayForwardChatStatusToString(
    GatewayForwardChatStatus status
) {
    switch (status) {
        case GatewayForwardChatStatus::
            kDelivered:
            return "delivered";


        case GatewayForwardChatStatus::
            kTargetNotLocal:
            return "target_not_local";


        case GatewayForwardChatStatus::
            kTargetNotConnected:
            return "target_not_connected";


        case GatewayForwardChatStatus::
            kDuplicateInProgress:
            return "duplicate_in_progress";


        case GatewayForwardChatStatus::
            kUnauthorized:
            return "unauthorized";


        case GatewayForwardChatStatus::
            kInvalidRequest:
            return "invalid_request";


        case GatewayForwardChatStatus::
            kInternalError:
            return "internal_error";
    }


    return "internal_error";
}


bool SerializeGatewayForwardChatRequest(
    const GatewayForwardChatRequest& request,
    std::string* output,
    std::string* error_message
) {
    if (output == nullptr) {
        SetError(
            error_message,
            "output is null"
        );

        return false;
    }


    if (
        !ValidateRequest(
            request,
            error_message
        )
    ) {
        return false;
    }


    Json body;


    body["message_id"] =
        request.message_id;


    body["source_gateway_id"] =
        request.source_gateway_id;


    body["source_lease_token"] =
        request.source_lease_token;


    body["from_user_id"] =
        request.from_user_id;


    body["to_user_id"] =
        request.to_user_id;


    body["message_body"] =
        request.message_body;


    *output = body.dump();


    if (error_message != nullptr) {
        error_message->clear();
    }


    return true;
}


bool DeserializeGatewayForwardChatRequest(
    const std::string& input,
    GatewayForwardChatRequest* request,
    std::string* error_message
) {
    if (request == nullptr) {
        SetError(
            error_message,
            "request is null"
        );

        return false;
    }


    try {
        const Json body =
            Json::parse(input);


        if (!body.is_object()) {
            SetError(
                error_message,
                "request body is not object"
            );

            return false;
        }


        GatewayForwardChatRequest parsed;


        if (
            !body.contains(
                "message_id"
            ) ||
            !body["message_id"].
                is_number_unsigned()
        ) {
            SetError(
                error_message,
                "invalid message_id"
            );

            return false;
        }


        if (
            !body.contains(
                "source_gateway_id"
            ) ||
            !body["source_gateway_id"].
                is_string()
        ) {
            SetError(
                error_message,
                "invalid source_gateway_id"
            );

            return false;
        }


        if (
            !body.contains(
                "source_lease_token"
            ) ||
            !body["source_lease_token"].
                is_string()
        ) {
            SetError(
                error_message,
                "invalid source_lease_token"
            );

            return false;
        }


        if (
            !body.contains(
                "from_user_id"
            ) ||
            !body["from_user_id"].
                is_number_integer()
        ) {
            SetError(
                error_message,
                "invalid from_user_id"
            );

            return false;
        }


        if (
            !body.contains(
                "to_user_id"
            ) ||
            !body["to_user_id"].
                is_number_integer()
        ) {
            SetError(
                error_message,
                "invalid to_user_id"
            );

            return false;
        }


        if (
            !body.contains(
                "message_body"
            ) ||
            !body["message_body"].
                is_string()
        ) {
            SetError(
                error_message,
                "invalid message_body"
            );

            return false;
        }


        parsed.message_id =
            body["message_id"].
                get<std::uint64_t>();


        parsed.source_gateway_id =
            body["source_gateway_id"].
                get<std::string>();


        parsed.source_lease_token =
            body["source_lease_token"].
                get<std::string>();


        parsed.from_user_id =
            body["from_user_id"].
                get<std::uint64_t>();


        parsed.to_user_id =
            body["to_user_id"].
                get<std::uint64_t>();


        parsed.message_body =
            body["message_body"].
                get<std::string>();


        if (
            !ValidateRequest(
                parsed,
                error_message
            )
        ) {
            return false;
        }


        *request =
            std::move(parsed);


        if (error_message != nullptr) {
            error_message->clear();
        }


        return true;
    } catch (
        const std::exception& error
    ) {
        SetError(
            error_message,
            std::string(
                "invalid request json: "
            ) +
                error.what()
        );

        return false;
    }
}


bool SerializeGatewayForwardChatResponse(
    const GatewayForwardChatResponse&
        response,
    std::string* output,
    std::string* error_message
) {
    if (output == nullptr) {
        SetError(
            error_message,
            "output is null"
        );

        return false;
    }


    if (
        ResponseRequiresMessageId(
            response.status
        ) &&
        response.message_id == 0
    ) {
        SetError(
            error_message,
            "message_id is zero"
        );

        return false;
    }


    if (
        ResponseRequiresTargetUser(
            response.status
        ) &&
        response.to_user_id == 0
    ) {
        SetError(
            error_message,
            "to_user_id is zero"
        );

        return false;
    }


    if (
        response.target_gateway_id.empty()
    ) {
        SetError(
            error_message,
            "target_gateway_id is empty"
        );

        return false;
    }


    Json body;


    body["status"] =
        GatewayForwardChatStatusToString(
            response.status
        );


    body["message_id"] =
        response.message_id;


    body["to_user_id"] =
        response.to_user_id;


    body["target_gateway_id"] =
        response.target_gateway_id;


    body["duplicate"] =
        response.duplicate;


    body["error_message"] =
        response.error_message;


    *output = body.dump();


    if (error_message != nullptr) {
        error_message->clear();
    }


    return true;
}


bool DeserializeGatewayForwardChatResponse(
    const std::string& input,
    GatewayForwardChatResponse* response,
    std::string* error_message
) {
    if (response == nullptr) {
        SetError(
            error_message,
            "response is null"
        );

        return false;
    }


    try {
        const Json body =
            Json::parse(input);


        if (!body.is_object()) {
            SetError(
                error_message,
                "response body is not object"
            );

            return false;
        }


        if (
            !body.contains("status") ||
            !body["status"].is_string()
        ) {
            SetError(
                error_message,
                "invalid status"
            );

            return false;
        }


        if (
            !body.contains(
                "message_id"
            ) ||
            !body["message_id"].
                is_number_unsigned()
        ) {
            SetError(
                error_message,
                "invalid message_id"
            );

            return false;
        }


        if (
            !body.contains(
                "to_user_id"
            ) ||
            !body["to_user_id"].
                is_number_integer()
        ) {
            SetError(
                error_message,
                "invalid to_user_id"
            );

            return false;
        }


        if (
            !body.contains(
                "target_gateway_id"
            ) ||
            !body["target_gateway_id"].
                is_string()
        ) {
            SetError(
                error_message,
                "invalid target_gateway_id"
            );

            return false;
        }


        if (
            !body.contains(
                "duplicate"
            ) ||
            !body["duplicate"].
                is_boolean()
        ) {
            SetError(
                error_message,
                "invalid duplicate"
            );

            return false;
        }


        GatewayForwardChatResponse parsed;


        const std::string status_text =
            body["status"].
                get<std::string>();


        if (
            !ParseStatus(
                status_text,
                &parsed.status
            )
        ) {
            SetError(
                error_message,
                "unknown gateway forward status"
            );

            return false;
        }


        parsed.message_id =
            body["message_id"].
                get<std::uint64_t>();


        parsed.to_user_id =
            body["to_user_id"].
                get<std::uint64_t>();


        parsed.target_gateway_id =
            body["target_gateway_id"].
                get<std::string>();


        parsed.duplicate =
            body["duplicate"].
                get<bool>();


        if (
            body.contains(
                "error_message"
            )
        ) {
            if (
                !body["error_message"].
                    is_string()
            ) {
                SetError(
                    error_message,
                    "invalid error_message"
                );

                return false;
            }


            parsed.error_message =
                body["error_message"].
                    get<std::string>();
        }


        if (
            ResponseRequiresMessageId(
                parsed.status
            ) &&
            parsed.message_id == 0
        ) {
            SetError(
                error_message,
                "message_id is zero"
            );

            return false;
        }


        if (
            ResponseRequiresTargetUser(
                parsed.status
            ) &&
            parsed.to_user_id == 0
        ) {
            SetError(
                error_message,
                "to_user_id is zero"
            );

            return false;
        }


        if (
            parsed.target_gateway_id.
                empty()
        ) {
            SetError(
                error_message,
                "target_gateway_id is empty"
            );

            return false;
        }


        *response =
            std::move(parsed);


        if (error_message != nullptr) {
            error_message->clear();
        }


        return true;
    } catch (
        const std::exception& error
    ) {
        SetError(
            error_message,
            std::string(
                "invalid response json: "
            ) +
                error.what()
        );

        return false;
    }
}

}  // namespace tinyimx