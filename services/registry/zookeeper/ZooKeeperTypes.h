#pragma once

#include <cstdint>
#include <string>

namespace tinyimx::registry::zookeeper {

enum class ConnectionState : std::uint8_t {
    kStopped = 0,
    kConnecting,
    kConnected,
    kDisconnected,
    kExpired,
    kAuthFailed,
};

enum class OperationStatus : std::uint8_t {
    kOk = 0,
    kNodeExists,
    kNoNode,
    kConnectionLoss,
    kSessionExpired,
    kAuthFailed,
    kNotConnected,
    kInvalidArgument,
    kError,
};

enum class WatchEventType : std::uint8_t {
    kUnknown = 0,
    kSession,
    kNodeCreated,
    kNodeDeleted,
    kNodeChanged,
    kChildrenChanged,
};

struct NodeRecord {
    std::string data;
    std::int64_t ephemeral_owner{0};
    int version{-1};
};

struct WatchEvent {
    WatchEventType type{WatchEventType::kUnknown};
    ConnectionState connection_state{ConnectionState::kStopped};
    std::string path;
    std::uint64_t session_generation{0};
};

[[nodiscard]] const char* ToString(ConnectionState state);
[[nodiscard]] const char* ToString(OperationStatus status);
[[nodiscard]] const char* ToString(WatchEventType type);

}  // namespace tinyimx::registry::zookeeper
