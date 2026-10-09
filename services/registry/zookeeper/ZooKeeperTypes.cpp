#include "services/registry/zookeeper/ZooKeeperTypes.h"

namespace tinyimx::registry::zookeeper {

const char* ToString(ConnectionState state) {
    switch (state) {
        case ConnectionState::kStopped:
            return "stopped";
        case ConnectionState::kConnecting:
            return "connecting";
        case ConnectionState::kConnected:
            return "connected";
        case ConnectionState::kDisconnected:
            return "disconnected";
        case ConnectionState::kExpired:
            return "expired";
        case ConnectionState::kAuthFailed:
            return "auth_failed";
        default:
            return "unknown";
    }
}

const char* ToString(OperationStatus status) {
    switch (status) {
        case OperationStatus::kOk:
            return "ok";
        case OperationStatus::kNodeExists:
            return "node_exists";
        case OperationStatus::kNoNode:
            return "no_node";
        case OperationStatus::kConnectionLoss:
            return "connection_loss";
        case OperationStatus::kSessionExpired:
            return "session_expired";
        case OperationStatus::kAuthFailed:
            return "auth_failed";
        case OperationStatus::kNotConnected:
            return "not_connected";
        case OperationStatus::kInvalidArgument:
            return "invalid_argument";
        case OperationStatus::kError:
            return "error";
        default:
            return "unknown";
    }
}

const char* ToString(WatchEventType type) {
    switch (type) {
        case WatchEventType::kSession:
            return "session";
        case WatchEventType::kNodeCreated:
            return "node_created";
        case WatchEventType::kNodeDeleted:
            return "node_deleted";
        case WatchEventType::kNodeChanged:
            return "node_changed";
        case WatchEventType::kChildrenChanged:
            return "children_changed";
        case WatchEventType::kUnknown:
        default:
            return "unknown";
    }
}

}  // namespace tinyimx::registry::zookeeper
