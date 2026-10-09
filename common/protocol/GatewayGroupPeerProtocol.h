#pragma once

#include <cstdint>
#include <string>

namespace tinyimx {

struct GatewayForwardGroupMessageRequest {
    std::uint64_t message_id{0};
    std::uint64_t recipient_user_id{0};
    std::string source_gateway_id;
    std::string source_lease_token;
};

enum class GatewayForwardGroupMessageStatus : std::uint32_t {
    kSubmitted = 0,
    kTargetNotLocal,
    kTargetNotConnected,
    kDuplicateInProgress,
    kAlreadyConfirmed,
    kUnauthorized,
    kInvalidRequest,
    kInternalError
};

struct GatewayForwardGroupMessageResponse {
    GatewayForwardGroupMessageStatus status{GatewayForwardGroupMessageStatus::kInternalError};
    std::uint64_t message_id{0};
    std::uint64_t recipient_user_id{0};
    std::string target_gateway_id;
    bool duplicate{false};
    std::string error_message;

    bool Submitted() const noexcept {
        return status == GatewayForwardGroupMessageStatus::kSubmitted ||
               status == GatewayForwardGroupMessageStatus::kAlreadyConfirmed;
    }
};

std::string GatewayForwardGroupMessageStatusToString(GatewayForwardGroupMessageStatus status);

bool SerializeGatewayForwardGroupMessageRequest(
    const GatewayForwardGroupMessageRequest& request,
    std::string* output,
    std::string* error_message = nullptr
);

bool DeserializeGatewayForwardGroupMessageRequest(
    const std::string& input,
    GatewayForwardGroupMessageRequest* request,
    std::string* error_message = nullptr
);

bool SerializeGatewayForwardGroupMessageResponse(
    const GatewayForwardGroupMessageResponse& response,
    std::string* output,
    std::string* error_message = nullptr
);

bool DeserializeGatewayForwardGroupMessageResponse(
    const std::string& input,
    GatewayForwardGroupMessageResponse* response,
    std::string* error_message = nullptr
);

}  // namespace tinyimx
