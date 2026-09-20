#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyimx {

enum class MessageType : std::uint16_t {
    kUnknown = 0,

    kLoginRequest = 1001,
    kLoginResponse = 1002,

    kChatMessage = 2001,
    kChatAck = 2002,

    kReadRequest = 2003,
    kReadResponse = 2004,

    kHistoryRequest = 2005,
    kHistoryResponse = 2006,

    kConversationListRequest = 2007,
    kConversationListResponse = 2008,

    kFriendListRequest = 2009,
    kFriendListResponse = 2010,

    kFriendRequestCreateRequest = 2011,
    kFriendRequestCreateResponse = 2012,

    kFriendRequestListRequest = 2013,
    kFriendRequestListResponse = 2014,

    kFriendRequestAcceptRequest = 2015,
    kFriendRequestAcceptResponse = 2016,

    kFriendRequestRejectRequest = 2017,
    kFriendRequestRejectResponse = 2018,

    /*
    * Gateway -> Receiver:
    *
    * kChatDelivery
    *     一次Receiver消息投递Attempt。
    *
    * Receiver -> Gateway:
    *
    * kChatDeliveryAck
    *     Receiver应用协议层确认收到Server Message。
    *
    * 注意：
    *
    * Packet.seq
    *     = Delivery Attempt Identity
    *
    * body.message_id
    *     = Stable Server Business Identity
    */
    kChatDelivery = 2019,
    kChatDeliveryAck = 2020,

    // Authenticated self-profile read path.
    kUserProfileRequest = 2021,
    kUserProfileResponse = 2022,

    // M17-A3 Group control-plane protocol.
    kCreateGroupRequest = 2023,
    kCreateGroupResponse = 2024,
    kGetGroupRequest = 2025,
    kGetGroupResponse = 2026,
    kUpdateGroupRequest = 2027,
    kUpdateGroupResponse = 2028,
    kDisbandGroupRequest = 2029,
    kDisbandGroupResponse = 2030,
    kJoinGroupRequest = 2031,
    kJoinGroupResponse = 2032,
    kLeaveGroupRequest = 2033,
    kLeaveGroupResponse = 2034,
    kInviteGroupMemberRequest = 2035,
    kInviteGroupMemberResponse = 2036,
    kKickGroupMemberRequest = 2037,
    kKickGroupMemberResponse = 2038,
    kSetGroupMemberRoleRequest = 2039,
    kSetGroupMemberRoleResponse = 2040,
    kSetGroupMemberMuteRequest = 2041,
    kSetGroupMemberMuteResponse = 2042,
    kTransferGroupOwnershipRequest = 2043,
    kTransferGroupOwnershipResponse = 2044,
    kListGroupMembersRequest = 2045,
    kListGroupMembersResponse = 2046,
    kListMyGroupsRequest = 2047,
    kListMyGroupsResponse = 2048,

    // M17-B1 reliable Group message durable-write vertical slice.
    kGroupMessageSendRequest = 2049,
    kGroupMessageSendResponse = 2050,

    // M17-B2 reliable receiver delivery protocol.
    kGroupMessageDelivery = 2051,
    kGroupMessageDeliveryAck = 2052,

    // Gateway-to-Gateway internal protocol.
    kGatewayForwardChatRequest = 3001,
    kGatewayForwardChatResponse = 3002,
    kGatewayForwardGroupMessageRequest = 3003,
    kGatewayForwardGroupMessageResponse = 3004,

    kHeartbeat = 9001,
    kError = 9999
};

struct PacketHeader {
    std::uint32_t magic{0};
    std::uint16_t version{0};
    std::uint16_t type{0};
    std::uint16_t flags{0};
    std::uint16_t reserved{0};
    std::uint32_t seq{0};
    std::uint32_t body_size{0};
};

struct Packet {
    MessageType type{MessageType::kUnknown};
    std::uint16_t flags{0};
    std::uint32_t seq{0};
    std::string body;
};

constexpr std::uint32_t kProtocolMagic = 0x54494D58;  // "TIMX"
constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kPacketHeaderSize = 20;
constexpr std::size_t kDefaultMaxBodySize = 1024 * 1024;

std::string MessageTypeToString(MessageType type);

bool IsKnownMessageType(MessageType type);
bool IsGatewayInternalMessageType(MessageType type);

}  // namespace tinyimx