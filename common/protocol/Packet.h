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

    // Gateway-to-Gateway internal protocol.
    kGatewayForwardChatRequest = 3001,
    kGatewayForwardChatResponse = 3002,

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