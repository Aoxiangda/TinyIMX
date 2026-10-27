#pragma once

#include "common/concurrency/ThreadPool.h"
#include "services/rpc/PrivateChatPermissionPort.h"
#include "services/social/application/FriendApplicationService.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>

namespace tinyimx::rpc {

struct LocalPrivateChatPermissionStats {
    std::uint64_t submitted{0};
    std::uint64_t executed{0};
    std::uint64_t rejected{0};
    std::uint64_t expired_before_execution{0};
    std::uint64_t expired_after_execution{0};
};
struct LocalPrivatePermissionAsyncStats {
    std::uint64_t started{0},finished{0},pending{0},peak_pending{0},deadline_completions{0};
};

// Same authoritative application/repository as SocialService; no allow cache.
// Database work uses the same bounded pool for sync and async calls. The async
// API starts one lazy deadline thread per client, releasing a Gateway worker
// while SQL runs. Expiry completes once; the accepted read retains its own
// state. Drain joins reads, deadline dispatch and active callbacks before
// dependencies may be destroyed. Call Drain outside completion callbacks.
class LocalPrivateChatPermissionClient final : public PrivateChatPermissionPort {
public:
    LocalPrivateChatPermissionClient(social::FriendApplicationService* application,
                                     std::size_t workers, std::size_t queued);
    ~LocalPrivateChatPermissionClient() override;

    [[nodiscard]] RpcResult<CheckPrivateChatPermissionRpcResponse>
    CheckPrivateChatPermission(const CheckPrivateChatPermissionRpcRequest& request,
                               const RpcCallOptions& options) override;
    [[nodiscard]] bool SupportsAsync() const noexcept override { return true; }
    void CheckPrivateChatPermissionAsync(const CheckPrivateChatPermissionRpcRequest&,
                                        const RpcCallOptions&,Completion) override;
    void Drain();
    [[nodiscard]] LocalPrivateChatPermissionStats Stats() const noexcept;
    [[nodiscard]] LocalPrivatePermissionAsyncStats AsyncStats() const;

private:
    social::FriendApplicationService* application_;
    ThreadPool pool_;
    const std::size_t async_capacity_;
    mutable std::mutex admission_;
    bool accepting_{true};
    std::once_flag drain_;
    std::atomic<std::uint64_t> submitted_{0}, executed_{0}, rejected_{0};
    std::atomic<std::uint64_t> expired_before_{0}, expired_after_{0};
    class AsyncState;
    // Allocated lazily: the existing synchronous/default path starts no timer.
    std::unique_ptr<AsyncState> async_;
};

}  // namespace tinyimx::rpc
