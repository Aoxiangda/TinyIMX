#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx {

using BusinessClock =
    std::chrono::steady_clock;

using BusinessTimePoint =
    BusinessClock::time_point;

using BusinessOrderingKey =
    std::uint64_t;

enum class BusinessSubmitStatus {
    kAccepted = 0,

    // Runtime全局达到容量。
    kOverloaded,

    // 某个ordering key所在stripe达到容量。
    kHotKeyOverloaded,

    // Submit时请求已经过期。
    kDeadlineExpired,

    // Runtime正在drain，不再接收新业务。
    kShuttingDown,

    // Task定义本身非法。
    kInvalidArgument
};

enum class BusinessCancellationPolicy {
    /*
     * 查询类任务。
     *
     * 如果Client/Session已经失效，
     * 且Task还没进入不可撤销业务阶段，
     * 可以不再执行。
     */
    kCancelable = 0,

    /*
     * 已承担durable responsibility的业务。
     *
     * 例如未来：
     * Chat commit之后、
     * Receiver ACK durable update等。
     */
    kMustRun
};

struct BusinessRequestContext {
    std::string operation;

    std::uint64_t user_id{0};

    std::uint32_t request_seq{0};

    /*
     * M13-A后续由SessionManager生成。
     *
     * 0表示没有Session fence要求。
     */
    std::uint64_t session_epoch{0};

    BusinessTimePoint received_at{
        BusinessClock::now()
    };

    /*
     * 默认constructed time_point{}：
     * 表示没有显式deadline。
     */
    BusinessTimePoint deadline{};

    std::optional<BusinessOrderingKey>
        ordering_key;

    bool HasDeadline() const noexcept {
        return
            deadline !=
            BusinessTimePoint{};
    }

    bool DeadlineExpired() const noexcept {
        return
            HasDeadline() &&
            BusinessClock::now() >=
                deadline;
    }

    std::chrono::milliseconds
    RemainingTime() const noexcept {
        if (!HasDeadline()) {
            return
                std::chrono::milliseconds::max();
        }

        const auto now =
            BusinessClock::now();

        if (now >= deadline) {
            return
                std::chrono::milliseconds{0};
        }

        return
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                deadline - now
            );
    }
};

const char* BusinessSubmitStatusToString(
    BusinessSubmitStatus status
) noexcept;

}  // namespace tinyimx