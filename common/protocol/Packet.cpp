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

        case MessageType::kChatDelivery:
            return "chat_delivery";

        case MessageType::kChatDeliveryAck:
            return "chat_delivery_ack";

        case MessageType::kUserProfileRequest:
            return "user_profile_request";
        case MessageType::kUserProfileResponse:
            return "user_profile_response";

        case MessageType::kCreateGroupRequest: return "create_group_request";
        case MessageType::kCreateGroupResponse: return "create_group_response";
        case MessageType::kGetGroupRequest: return "get_group_request";
        case MessageType::kGetGroupResponse: return "get_group_response";
        case MessageType::kUpdateGroupRequest: return "update_group_request";
        case MessageType::kUpdateGroupResponse: return "update_group_response";
        case MessageType::kDisbandGroupRequest: return "disband_group_request";
        case MessageType::kDisbandGroupResponse: return "disband_group_response";
        case MessageType::kJoinGroupRequest: return "join_group_request";
        case MessageType::kJoinGroupResponse: return "join_group_response";
        case MessageType::kLeaveGroupRequest: return "leave_group_request";
        case MessageType::kLeaveGroupResponse: return "leave_group_response";
        case MessageType::kInviteGroupMemberRequest: return "invite_group_member_request";
        case MessageType::kInviteGroupMemberResponse: return "invite_group_member_response";
        case MessageType::kKickGroupMemberRequest: return "kick_group_member_request";
        case MessageType::kKickGroupMemberResponse: return "kick_group_member_response";
        case MessageType::kSetGroupMemberRoleRequest: return "set_group_member_role_request";
        case MessageType::kSetGroupMemberRoleResponse: return "set_group_member_role_response";
        case MessageType::kSetGroupMemberMuteRequest: return "set_group_member_mute_request";
        case MessageType::kSetGroupMemberMuteResponse: return "set_group_member_mute_response";
        case MessageType::kTransferGroupOwnershipRequest: return "transfer_group_ownership_request";
        case MessageType::kTransferGroupOwnershipResponse: return "transfer_group_ownership_response";
        case MessageType::kListGroupMembersRequest: return "list_group_members_request";
        case MessageType::kListGroupMembersResponse: return "list_group_members_response";
        case MessageType::kListMyGroupsRequest: return "list_my_groups_request";
        case MessageType::kListMyGroupsResponse: return "list_my_groups_response";
        case MessageType::kGroupMessageSendRequest: return "group_message_send_request";
        case MessageType::kGroupMessageSendResponse: return "group_message_send_response";
        case MessageType::kGroupMessageDelivery: return "group_message_delivery";
        case MessageType::kGroupMessageDeliveryAck: return "group_message_delivery_ack";

        case MessageType::kGatewayForwardChatRequest:
            return "gateway_forward_chat_request";
        case MessageType::kGatewayForwardChatResponse:
            return "gateway_forward_chat_response";
        case MessageType::kGatewayForwardGroupMessageRequest:
            return "gateway_forward_group_message_request";
        case MessageType::kGatewayForwardGroupMessageResponse:
            return "gateway_forward_group_message_response";

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

        case MessageType::kChatDelivery:
        case MessageType::kChatDeliveryAck:

        case MessageType::kUserProfileRequest:
        case MessageType::kUserProfileResponse:

        case MessageType::kCreateGroupRequest:
        case MessageType::kCreateGroupResponse:
        case MessageType::kGetGroupRequest:
        case MessageType::kGetGroupResponse:
        case MessageType::kUpdateGroupRequest:
        case MessageType::kUpdateGroupResponse:
        case MessageType::kDisbandGroupRequest:
        case MessageType::kDisbandGroupResponse:
        case MessageType::kJoinGroupRequest:
        case MessageType::kJoinGroupResponse:
        case MessageType::kLeaveGroupRequest:
        case MessageType::kLeaveGroupResponse:
        case MessageType::kInviteGroupMemberRequest:
        case MessageType::kInviteGroupMemberResponse:
        case MessageType::kKickGroupMemberRequest:
        case MessageType::kKickGroupMemberResponse:
        case MessageType::kSetGroupMemberRoleRequest:
        case MessageType::kSetGroupMemberRoleResponse:
        case MessageType::kSetGroupMemberMuteRequest:
        case MessageType::kSetGroupMemberMuteResponse:
        case MessageType::kTransferGroupOwnershipRequest:
        case MessageType::kTransferGroupOwnershipResponse:
        case MessageType::kListGroupMembersRequest:
        case MessageType::kListGroupMembersResponse:
        case MessageType::kListMyGroupsRequest:
        case MessageType::kListMyGroupsResponse:
        case MessageType::kGroupMessageSendRequest:
        case MessageType::kGroupMessageSendResponse:
        case MessageType::kGroupMessageDelivery:
        case MessageType::kGroupMessageDeliveryAck:

        case MessageType::kGatewayForwardChatRequest:
        case MessageType::kGatewayForwardChatResponse:
        case MessageType::kGatewayForwardGroupMessageRequest:
        case MessageType::kGatewayForwardGroupMessageResponse:

        case MessageType::kHeartbeat:
        case MessageType::kError:
            return true;
        default:
            return false;
    }
}

bool IsGatewayInternalMessageType(MessageType type) {
    switch (type) {
        case MessageType::
            kGatewayForwardChatRequest:

        case MessageType::
            kGatewayForwardChatResponse:
        case MessageType::
            kGatewayForwardGroupMessageRequest:
        case MessageType::
            kGatewayForwardGroupMessageResponse:
            return true;

        default:
            return false;
    }
}

}  // namespace tinyimx