#include "services/registry/zookeeper/ZooKeeperClient.h"

#include "common/logging/LogMacros.h"

#include <zookeeper/zookeeper.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace tinyimx::registry::zookeeper {
namespace {

constexpr auto kRecoveryRetryDelay =
    std::chrono::milliseconds(200);

bool ValidAbsolutePath(const std::string& path) {
    return !path.empty() &&
           path.front() == '/' &&
           path.find('\0') == std::string::npos;
}

std::vector<std::string> ParentPrefixes(
    const std::string& path
) {
    std::vector<std::string> result;
    if (!ValidAbsolutePath(path) || path == "/") {
        return result;
    }

    std::string current;
    std::size_t cursor = 1;
    while (cursor <= path.size()) {
        const std::size_t slash = path.find('/', cursor);
        const std::string part = path.substr(
            cursor,
            slash == std::string::npos
                ? std::string::npos
                : slash - cursor
        );
        if (!part.empty()) {
            current += "/" + part;
            result.push_back(current);
        }
        if (slash == std::string::npos) {
            break;
        }
        cursor = slash + 1;
    }
    return result;
}

}  // namespace

ZooKeeperClient::~ZooKeeperClient() {
    Stop();
}

bool ZooKeeperClient::Start(
    const ZooKeeperConfig& config
) {
    Stop();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        config_ = config;
        state_ = ConnectionState::kConnecting;
        running_ = true;
        stopping_ = false;
        session_generation_ = 0;
        session_id_ = 0;
        last_error_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        if (!CreateHandleLocked()) {
            std::lock_guard<std::mutex> state_lock(state_mutex_);
            running_ = false;
            state_ = ConnectionState::kStopped;
            return false;
        }
    }

    recovery_thread_ = std::thread(
        &ZooKeeperClient::RecoveryLoop,
        this
    );

    if (!WaitUntilConnected(
            std::chrono::milliseconds(
                config_.connect_timeout_ms
            )
        )) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (last_error_.empty()) {
                last_error_ =
                    "ZooKeeper connection timeout";
            }
        }
        Stop();
        return false;
    }

    return true;
}

void ZooKeeperClient::Stop() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!running_ && !recovery_thread_.joinable()) {
            state_ = ConnectionState::kStopped;
            return;
        }
        stopping_ = true;
        running_ = false;
        state_cv_.notify_all();
    }

    if (recovery_thread_.joinable()) {
        recovery_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        CloseHandleLocked();
    }

    {
        std::lock_guard<std::mutex> lock(observer_mutex_);
        observers_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = ConnectionState::kStopped;
        session_id_ = 0;
        state_cv_.notify_all();
    }
}

bool ZooKeeperClient::Running() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return running_;
}

bool ZooKeeperClient::Connected() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_ == ConnectionState::kConnected;
}

ConnectionState ZooKeeperClient::State() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

std::uint64_t ZooKeeperClient::SessionGeneration() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return session_generation_;
}

std::int64_t ZooKeeperClient::SessionId() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return session_id_;
}

std::string ZooKeeperClient::LastError() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

bool ZooKeeperClient::WaitUntilConnected(
    std::chrono::milliseconds timeout
) const {
    std::unique_lock<std::mutex> lock(state_mutex_);
    const bool signaled = state_cv_.wait_for(
        lock,
        timeout,
        [this]() {
            return state_ == ConnectionState::kConnected ||
                   state_ == ConnectionState::kAuthFailed ||
                   stopping_;
        }
    );
    return signaled &&
           state_ == ConnectionState::kConnected;
}

OperationStatus ZooKeeperClient::EnsurePersistentPath(
    const std::string& path
) {
    if (!ValidAbsolutePath(path)) {
        return OperationStatus::kInvalidArgument;
    }

    const auto prefixes = ParentPrefixes(path);
    std::lock_guard<std::mutex> lock(operation_mutex_);
    if (handle_ == nullptr) {
        return OperationStatus::kNotConnected;
    }

    for (const auto& prefix : prefixes) {
        char created_path[1024] = {};
        const int rc = zoo_create(
            handle_,
            prefix.c_str(),
            "",
            0,
            &ZOO_OPEN_ACL_UNSAFE,
            0,
            created_path,
            static_cast<int>(sizeof(created_path))
        );
        if (rc == ZOK || rc == ZNODEEXISTS) {
            continue;
        }
        return TranslateResult(rc);
    }
    return OperationStatus::kOk;
}

OperationStatus ZooKeeperClient::CreateEphemeral(
    const std::string& path,
    const std::string& data
) {
    if (!ValidAbsolutePath(path) || data.size() > 1024 * 1024) {
        return OperationStatus::kInvalidArgument;
    }

    std::lock_guard<std::mutex> lock(operation_mutex_);
    if (handle_ == nullptr) {
        return OperationStatus::kNotConnected;
    }

    char created_path[1024] = {};
    const int rc = zoo_create(
        handle_,
        path.c_str(),
        data.data(),
        static_cast<int>(data.size()),
        &ZOO_OPEN_ACL_UNSAFE,
        ZOO_EPHEMERAL,
        created_path,
        static_cast<int>(sizeof(created_path))
    );
    return TranslateResult(rc);
}

OperationStatus ZooKeeperClient::GetNode(
    const std::string& path,
    NodeRecord* output
) {
    if (!ValidAbsolutePath(path) || output == nullptr) {
        return OperationStatus::kInvalidArgument;
    }

    std::lock_guard<std::mutex> lock(operation_mutex_);
    if (handle_ == nullptr) {
        return OperationStatus::kNotConnected;
    }

    Stat initial_stat{};
    int rc = zoo_exists(
        handle_,
        path.c_str(),
        0,
        &initial_stat
    );
    if (rc != ZOK) {
        return TranslateResult(rc);
    }

    int buffer_length = std::max(
        initial_stat.dataLength,
        0
    );
    std::vector<char> buffer(
        static_cast<std::size_t>(
            std::max(buffer_length, 1)
        )
    );

    Stat stat{};
    rc = zoo_get(
        handle_,
        path.c_str(),
        0,
        buffer.data(),
        &buffer_length,
        &stat
    );
    if (rc != ZOK) {
        return TranslateResult(rc);
    }

    output->data.assign(
        buffer.data(),
        static_cast<std::size_t>(
            std::max(buffer_length, 0)
        )
    );
    output->ephemeral_owner =
        static_cast<std::int64_t>(stat.ephemeralOwner);
    output->version = stat.version;
    return OperationStatus::kOk;
}

OperationStatus ZooKeeperClient::DeleteNode(
    const std::string& path,
    int version
) {
    if (!ValidAbsolutePath(path)) {
        return OperationStatus::kInvalidArgument;
    }

    std::lock_guard<std::mutex> lock(operation_mutex_);
    if (handle_ == nullptr) {
        return OperationStatus::kNotConnected;
    }

    return TranslateResult(
        zoo_delete(
            handle_,
            path.c_str(),
            version
        )
    );
}

OperationStatus ZooKeeperClient::GetChildren(
    const std::string& path,
    bool watch,
    std::vector<std::string>* children
) {
    if (!ValidAbsolutePath(path) || children == nullptr) {
        return OperationStatus::kInvalidArgument;
    }

    children->clear();
    std::lock_guard<std::mutex> lock(operation_mutex_);
    if (handle_ == nullptr) {
        return OperationStatus::kNotConnected;
    }

    String_vector raw_children{};
    const int rc = zoo_get_children(
        handle_,
        path.c_str(),
        watch ? 1 : 0,
        &raw_children
    );
    if (rc != ZOK) {
        return TranslateResult(rc);
    }

    children->reserve(
        static_cast<std::size_t>(
            std::max(raw_children.count, 0)
        )
    );
    for (int index = 0; index < raw_children.count; ++index) {
        const char* value = raw_children.data[index];
        if (value != nullptr) {
            children->emplace_back(value);
        }
    }
    deallocate_String_vector(&raw_children);
    return OperationStatus::kOk;
}

OperationStatus ZooKeeperClient::Exists(
    const std::string& path,
    bool watch,
    bool* exists
) {
    if (!ValidAbsolutePath(path) || exists == nullptr) {
        return OperationStatus::kInvalidArgument;
    }

    *exists = false;
    std::lock_guard<std::mutex> lock(operation_mutex_);
    if (handle_ == nullptr) {
        return OperationStatus::kNotConnected;
    }

    Stat stat{};
    const int rc = zoo_exists(
        handle_,
        path.c_str(),
        watch ? 1 : 0,
        &stat
    );
    if (rc == ZOK) {
        *exists = true;
        return OperationStatus::kOk;
    }
    if (rc == ZNONODE) {
        // zoo_exists installs an existence watch even when the node is absent.
        *exists = false;
        return OperationStatus::kOk;
    }
    return TranslateResult(rc);
}

ZooKeeperClient::WatchObserverId ZooKeeperClient::AddWatchObserver(
    WatchEventCallback callback
) {
    if (!callback) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(observer_mutex_);
    WatchObserverId observer_id = next_observer_id_++;
    if (observer_id == 0) {
        observer_id = next_observer_id_++;
    }
    observers_.emplace(observer_id, std::move(callback));
    return observer_id;
}

void ZooKeeperClient::RemoveWatchObserver(
    WatchObserverId observer_id
) {
    if (observer_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(observer_mutex_);
    observers_.erase(observer_id);
}

void ZooKeeperClient::GlobalWatcher(
    zhandle_t* handle,
    int type,
    int state,
    const char* path,
    void* context
) {
    if (context == nullptr) {
        return;
    }
    static_cast<ZooKeeperClient*>(context)->OnWatcher(
        handle,
        type,
        state,
        path
    );
}

void ZooKeeperClient::OnWatcher(
    zhandle_t* handle,
    int type,
    int state,
    const char* path
) {
    WatchEvent event;
    event.type = TranslateWatchEventType(type);
    if (path != nullptr) {
        event.path = path;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (stopping_) {
            return;
        }

        if (type == ZOO_SESSION_EVENT) {
            if (state == ZOO_CONNECTED_STATE) {
                std::int64_t new_session_id = 0;
                const clientid_t* client_id = zoo_client_id(handle);
                if (client_id != nullptr) {
                    new_session_id = static_cast<std::int64_t>(
                        client_id->client_id
                    );
                }

                if (session_generation_ == 0 ||
                    (new_session_id != 0 &&
                     new_session_id != session_id_)) {
                    ++session_generation_;
                }
                if (new_session_id != 0) {
                    session_id_ = new_session_id;
                }
                state_ = ConnectionState::kConnected;
                last_error_.clear();
            } else if (state == ZOO_EXPIRED_SESSION_STATE) {
                state_ = ConnectionState::kExpired;
                last_error_ = "ZooKeeper session expired";
            } else if (state == ZOO_AUTH_FAILED_STATE) {
                state_ = ConnectionState::kAuthFailed;
                last_error_ = "ZooKeeper authentication failed";
            } else if (state == ZOO_CONNECTING_STATE ||
                       state == ZOO_ASSOCIATING_STATE) {
                state_ = ConnectionState::kDisconnected;
            } else {
                state_ = ConnectionState::kDisconnected;
            }

            state_cv_.notify_all();
        }

        event.connection_state = state_;
        event.session_generation = session_generation_;
    }

    if (type == ZOO_SESSION_EVENT) {
        if (event.connection_state == ConnectionState::kConnected) {
            LOG_INFO(
                "ZooKeeper session connected"
                << ", session_id=" << SessionId()
                << ", generation=" << event.session_generation
            );
        } else if (event.connection_state == ConnectionState::kExpired) {
            LOG_WARN(
                "ZooKeeper session expired"
                << ", generation=" << event.session_generation
            );
        }
    }

    DispatchWatchEvent(event);
}

void ZooKeeperClient::DispatchWatchEvent(
    const WatchEvent& event
) {
    std::vector<WatchEventCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(observer_mutex_);
        callbacks.reserve(observers_.size());
        for (const auto& [_, callback] : observers_) {
            callbacks.push_back(callback);
        }
    }

    for (const auto& callback : callbacks) {
        if (callback) {
            callback(event);
        }
    }
}

void ZooKeeperClient::RecoveryLoop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            state_cv_.wait(
                lock,
                [this]() {
                    return stopping_ ||
                           state_ == ConnectionState::kExpired;
                }
            );
            if (stopping_) {
                return;
            }
            state_ = ConnectionState::kConnecting;
            state_cv_.notify_all();
        }

        bool created = false;
        {
            std::lock_guard<std::mutex> lock(operation_mutex_);
            CloseHandleLocked();
            created = CreateHandleLocked();
        }

        if (!created) {
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (stopping_) {
                    return;
                }
                state_ = ConnectionState::kExpired;
            }
            std::this_thread::sleep_for(kRecoveryRetryDelay);
            continue;
        }

        // The C client performs reconnect asynchronously. The watcher will
        // transition us to Connected or back to Expired when appropriate.
        std::unique_lock<std::mutex> lock(state_mutex_);
        state_cv_.wait_for(
            lock,
            std::chrono::milliseconds(
                config_.connect_timeout_ms
            ),
            [this]() {
                return stopping_ ||
                       state_ == ConnectionState::kConnected ||
                       state_ == ConnectionState::kExpired ||
                       state_ == ConnectionState::kAuthFailed;
            }
        );
        if (stopping_ || state_ == ConnectionState::kAuthFailed) {
            return;
        }
    }
}

bool ZooKeeperClient::CreateHandleLocked() {
    handle_ = zookeeper_init(
        config_.connect_string.c_str(),
        &ZooKeeperClient::GlobalWatcher,
        config_.session_timeout_ms,
        nullptr,
        this,
        0
    );

    if (handle_ == nullptr) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        SetErrorLocked("zookeeper_init returned null");
        return false;
    }
    return true;
}

void ZooKeeperClient::CloseHandleLocked() {
    if (handle_ != nullptr) {
        zookeeper_close(handle_);
        handle_ = nullptr;
    }
}

void ZooKeeperClient::SetErrorLocked(
    const std::string& error
) {
    last_error_ = error;
}

OperationStatus ZooKeeperClient::TranslateResult(int rc) const {
    switch (rc) {
        case ZOK:
            return OperationStatus::kOk;
        case ZNODEEXISTS:
            return OperationStatus::kNodeExists;
        case ZNONODE:
            return OperationStatus::kNoNode;
        case ZCONNECTIONLOSS:
            return OperationStatus::kConnectionLoss;
        case ZSESSIONEXPIRED:
            return OperationStatus::kSessionExpired;
        case ZAUTHFAILED:
            return OperationStatus::kAuthFailed;
        default:
            return OperationStatus::kError;
    }
}

WatchEventType ZooKeeperClient::TranslateWatchEventType(int type) {
    // ZooKeeper 3.8.x exposes ZOO_*_EVENT as extern const int in the C API.
    // They are runtime constants, not C++ integral constant expressions, so
    // they cannot be used as switch case labels. Compare them directly.
    if (type == ZOO_SESSION_EVENT) {
        return WatchEventType::kSession;
    }
    if (type == ZOO_CREATED_EVENT) {
        return WatchEventType::kNodeCreated;
    }
    if (type == ZOO_DELETED_EVENT) {
        return WatchEventType::kNodeDeleted;
    }
    if (type == ZOO_CHANGED_EVENT) {
        return WatchEventType::kNodeChanged;
    }
    if (type == ZOO_CHILD_EVENT) {
        return WatchEventType::kChildrenChanged;
    }
    return WatchEventType::kUnknown;
}

}  // namespace tinyimx::registry::zookeeper
