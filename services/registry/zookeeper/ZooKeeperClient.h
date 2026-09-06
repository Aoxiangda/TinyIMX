#pragma once

#include "common/config/ConfigTypes.h"
#include "services/registry/zookeeper/ZooKeeperTypes.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct _zhandle;
typedef struct _zhandle zhandle_t;

namespace tinyimx::registry::zookeeper {

class ZooKeeperClient final {
public:
    using WatchObserverId = std::uint64_t;
    using WatchEventCallback = std::function<void(const WatchEvent&)>;

    ZooKeeperClient() = default;
    ~ZooKeeperClient();

    ZooKeeperClient(const ZooKeeperClient&) = delete;
    ZooKeeperClient& operator=(const ZooKeeperClient&) = delete;

    bool Start(const ZooKeeperConfig& config);
    void Stop();

    [[nodiscard]] bool Running() const;
    [[nodiscard]] bool Connected() const;
    [[nodiscard]] ConnectionState State() const;
    [[nodiscard]] std::uint64_t SessionGeneration() const;
    [[nodiscard]] std::int64_t SessionId() const;
    [[nodiscard]] std::string LastError() const;

    bool WaitUntilConnected(std::chrono::milliseconds timeout) const;

    OperationStatus EnsurePersistentPath(const std::string& path);
    OperationStatus CreateEphemeral(
        const std::string& path,
        const std::string& data
    );
    OperationStatus GetNode(
        const std::string& path,
        NodeRecord* output
    );
    OperationStatus DeleteNode(
        const std::string& path,
        int version = -1
    );

    // M15-B discovery primitives. Standard ZooKeeper watches are one-shot;
    // callers must re-read with watch=true after receiving an event.
    OperationStatus GetChildren(
        const std::string& path,
        bool watch,
        std::vector<std::string>* children
    );
    OperationStatus Exists(
        const std::string& path,
        bool watch,
        bool* exists
    );

    WatchObserverId AddWatchObserver(WatchEventCallback callback);
    void RemoveWatchObserver(WatchObserverId observer_id);

private:
    static void GlobalWatcher(
        zhandle_t* handle,
        int type,
        int state,
        const char* path,
        void* context
    );

    void OnWatcher(
        zhandle_t* handle,
        int type,
        int state,
        const char* path
    );
    void DispatchWatchEvent(const WatchEvent& event);

    void RecoveryLoop();
    bool CreateHandleLocked();
    void CloseHandleLocked();
    void SetErrorLocked(const std::string& error);

    [[nodiscard]] OperationStatus TranslateResult(int rc) const;
    [[nodiscard]] static WatchEventType TranslateWatchEventType(int type);

private:
    ZooKeeperConfig config_;

    mutable std::mutex state_mutex_;
    mutable std::condition_variable state_cv_;
    ConnectionState state_{ConnectionState::kStopped};
    bool running_{false};
    bool stopping_{false};
    std::uint64_t session_generation_{0};
    std::int64_t session_id_{0};
    std::string last_error_;

    // Serializes all synchronous C API calls against handle close/recreate.
    mutable std::mutex operation_mutex_;
    zhandle_t* handle_{nullptr};

    // Observer callbacks are copied under this lock and invoked after release.
    // Discovery callbacks must be lightweight (enqueue + notify only).
    mutable std::mutex observer_mutex_;
    WatchObserverId next_observer_id_{1};
    std::unordered_map<WatchObserverId, WatchEventCallback> observers_;

    std::thread recovery_thread_;
};

}  // namespace tinyimx::registry::zookeeper
