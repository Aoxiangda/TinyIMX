#include "common/protocol/Packet.h"

namespace tinyimx {

std::string MessageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::kUnknown:
            return "unknown";

        case MessageType::kLoginRequest:
            return "login_request";
        case MessageType::kLoginResponse:
            return "login_response";

        case MessageType::kChatMessage:
            return "chat_message";
        case MessageType::kChatAck:
            return "chat_ack";

        case MessageType::kReadRequest:
            return "read_request";
        case MessageType::kReadResponse:
            return "read_response";

        case MessageType::kHistoryRequest:
            return "history_request";
        case MessageType::kHistoryResponse:
            return "history_response";

        case MessageType::kFriendListRequest:
            return "friend_list_request";
        case MessageType::kFriendListResponse:
            return "friend_list_response";

        case MessageType::kConversationListRequest:
            return "conversation_list_request";
        case MessageType::kConversationListResponse:
            return "conversation_list_response";

        case MessageType::kFriendRequestCreateRequest:
            return "friend_request_create_request";
        case MessageType::kFriendRequestCreateResponse:
            return "friend_request_create_response";

        case MessageType::kFriendRequestListRequest:
            return "friend_request_list_request";
        case MessageType::kFriendRequestListResponse:
            return "friend_request_list_response";

        case MessageType::kFriendRequestAcceptRequest:
            return "friend_request_accept_request";
        case MessageType::kFriendRequestAcceptResponse:
            return "friend_request_accept_response";

        case MessageType::kFriendRequestRejectRequest:
            return "friend_request_reject_request";
        case MessageType::kFriendRequestRejectResponse:
            return "friend_request_reject_response";

        case MessageType::kHeartbeat:
            return "heartbeat";
        case MessageType::kError:
            return "error";

        default:
            return "invalid";
    }
}

bool IsKnownMessageType(MessageType type) {
    switch (type) {
        case MessageType::kLoginRequest:
        case MessageType::kLoginResponse:

        case MessageType::kChatMessage:
        case MessageType::kChatAck:

        case MessageType::kReadRequest:
        case MessageType::kReadResponse:

        case MessageType::kHistoryRequest:
        case MessageType::kHistoryResponse:

        case MessageType::kFriendListRequest:
        case MessageType::kFriendListResponse:

        case MessageType::kConversationListRequest:
        case MessageType::kConversationListResponse:

        case MessageType::kFriendRequestCreateRequest:
        case MessageType::kFriendRequestCreateResponse:

        case MessageType::kFriendRequestListRequest:
        case MessageType::kFriendRequestListResponse:

        case MessageType::kFriendRequestAcceptRequest:
        case MessageType::kFriendRequestAcceptResponse:

        case MessageType::kFriendRequestRejectRequest:
        case MessageType::kFriendRequestRejectResponse:

        case MessageType::kHeartbeat:
        case MessageType::kError:
            return true;
        default:
            return false;
    }
}

}  // namespace tinyimx