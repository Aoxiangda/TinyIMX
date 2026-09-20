#include "common/protocol/GatewayGroupPeerProtocol.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <utility>

namespace tinyimx {
namespace {
using Json = nlohmann::json;
void SetError(std::string* out, std::string value) { if (out) *out = std::move(value); }

bool ParseStatus(const std::string& value, GatewayForwardGroupMessageStatus* out) {
    if (!out) return false;
    if (value == "submitted") *out = GatewayForwardGroupMessageStatus::kSubmitted;
    else if (value == "target_not_local") *out = GatewayForwardGroupMessageStatus::kTargetNotLocal;
    else if (value == "target_not_connected") *out = GatewayForwardGroupMessageStatus::kTargetNotConnected;
    else if (value == "duplicate_in_progress") *out = GatewayForwardGroupMessageStatus::kDuplicateInProgress;
    else if (value == "already_confirmed") *out = GatewayForwardGroupMessageStatus::kAlreadyConfirmed;
    else if (value == "unauthorized") *out = GatewayForwardGroupMessageStatus::kUnauthorized;
    else if (value == "invalid_request") *out = GatewayForwardGroupMessageStatus::kInvalidRequest;
    else if (value == "internal_error") *out = GatewayForwardGroupMessageStatus::kInternalError;
    else return false;
    return true;
}

bool ValidateRequest(const GatewayForwardGroupMessageRequest& r, std::string* error) {
    if (r.message_id == 0) { SetError(error, "message_id is zero"); return false; }
    if (r.recipient_user_id == 0) { SetError(error, "recipient_user_id is zero"); return false; }
    if (r.source_gateway_id.empty()) { SetError(error, "source_gateway_id is empty"); return false; }
    if (r.source_lease_token.empty()) { SetError(error, "source_lease_token is empty"); return false; }
    return true;
}
}  // namespace

std::string GatewayForwardGroupMessageStatusToString(GatewayForwardGroupMessageStatus status) {
    switch (status) {
        case GatewayForwardGroupMessageStatus::kSubmitted: return "submitted";
        case GatewayForwardGroupMessageStatus::kTargetNotLocal: return "target_not_local";
        case GatewayForwardGroupMessageStatus::kTargetNotConnected: return "target_not_connected";
        case GatewayForwardGroupMessageStatus::kDuplicateInProgress: return "duplicate_in_progress";
        case GatewayForwardGroupMessageStatus::kAlreadyConfirmed: return "already_confirmed";
        case GatewayForwardGroupMessageStatus::kUnauthorized: return "unauthorized";
        case GatewayForwardGroupMessageStatus::kInvalidRequest: return "invalid_request";
        case GatewayForwardGroupMessageStatus::kInternalError: return "internal_error";
    }
    return "internal_error";
}

bool SerializeGatewayForwardGroupMessageRequest(
    const GatewayForwardGroupMessageRequest& request, std::string* output, std::string* error_message) {
    if (!output) { SetError(error_message, "output is null"); return false; }
    if (!ValidateRequest(request, error_message)) return false;
    Json body;
    body["message_id"] = request.message_id;
    body["recipient_user_id"] = request.recipient_user_id;
    body["source_gateway_id"] = request.source_gateway_id;
    body["source_lease_token"] = request.source_lease_token;
    *output = body.dump();
    if (error_message) error_message->clear();
    return true;
}

bool DeserializeGatewayForwardGroupMessageRequest(
    const std::string& input, GatewayForwardGroupMessageRequest* request, std::string* error_message) {
    if (!request) { SetError(error_message, "request is null"); return false; }
    try {
        const Json body = Json::parse(input);
        if (!body.is_object() ||
            !body.contains("message_id") || !body["message_id"].is_number_unsigned() ||
            !body.contains("recipient_user_id") || !body["recipient_user_id"].is_number_unsigned() ||
            !body.contains("source_gateway_id") || !body["source_gateway_id"].is_string() ||
            !body.contains("source_lease_token") || !body["source_lease_token"].is_string()) {
            SetError(error_message, "invalid gateway group request body"); return false;
        }
        GatewayForwardGroupMessageRequest parsed;
        parsed.message_id = body["message_id"].get<std::uint64_t>();
        parsed.recipient_user_id = body["recipient_user_id"].get<std::uint64_t>();
        parsed.source_gateway_id = body["source_gateway_id"].get<std::string>();
        parsed.source_lease_token = body["source_lease_token"].get<std::string>();
        if (!ValidateRequest(parsed, error_message)) return false;
        *request = std::move(parsed);
        if (error_message) error_message->clear();
        return true;
    } catch (const std::exception& e) {
        SetError(error_message, std::string("invalid gateway group request json: ") + e.what()); return false;
    }
}

bool SerializeGatewayForwardGroupMessageResponse(
    const GatewayForwardGroupMessageResponse& response, std::string* output, std::string* error_message) {
    if (!output) { SetError(error_message, "output is null"); return false; }
    if (response.message_id == 0 || response.recipient_user_id == 0 || response.target_gateway_id.empty()) {
        SetError(error_message, "invalid gateway group response identity"); return false;
    }
    Json body;
    body["status"] = GatewayForwardGroupMessageStatusToString(response.status);
    body["message_id"] = response.message_id;
    body["recipient_user_id"] = response.recipient_user_id;
    body["target_gateway_id"] = response.target_gateway_id;
    body["duplicate"] = response.duplicate;
    body["error_message"] = response.error_message;
    *output = body.dump();
    if (error_message) error_message->clear();
    return true;
}

bool DeserializeGatewayForwardGroupMessageResponse(
    const std::string& input, GatewayForwardGroupMessageResponse* response, std::string* error_message) {
    if (!response) { SetError(error_message, "response is null"); return false; }
    try {
        const Json body = Json::parse(input);
        if (!body.is_object() ||
            !body.contains("status") || !body["status"].is_string() ||
            !body.contains("message_id") || !body["message_id"].is_number_unsigned() ||
            !body.contains("recipient_user_id") || !body["recipient_user_id"].is_number_unsigned() ||
            !body.contains("target_gateway_id") || !body["target_gateway_id"].is_string() ||
            !body.contains("duplicate") || !body["duplicate"].is_boolean()) {
            SetError(error_message, "invalid gateway group response body"); return false;
        }
        GatewayForwardGroupMessageResponse parsed;
        if (!ParseStatus(body["status"].get<std::string>(), &parsed.status)) {
            SetError(error_message, "unknown gateway group response status"); return false;
        }
        parsed.message_id = body["message_id"].get<std::uint64_t>();
        parsed.recipient_user_id = body["recipient_user_id"].get<std::uint64_t>();
        parsed.target_gateway_id = body["target_gateway_id"].get<std::string>();
        parsed.duplicate = body["duplicate"].get<bool>();
        if (body.contains("error_message")) {
            if (!body["error_message"].is_string()) { SetError(error_message, "invalid error_message"); return false; }
            parsed.error_message = body["error_message"].get<std::string>();
        }
        if (parsed.message_id == 0 || parsed.recipient_user_id == 0 || parsed.target_gateway_id.empty()) {
            SetError(error_message, "invalid gateway group response identity"); return false;
        }
        *response = std::move(parsed);
        if (error_message) error_message->clear();
        return true;
    } catch (const std::exception& e) {
        SetError(error_message, std::string("invalid gateway group response json: ") + e.what()); return false;
    }
}

}  // namespace tinyimx
