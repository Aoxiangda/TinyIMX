#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace tinyimx::rpc {

enum class RpcErrorCode {
    kOk = 0,
    kInvalidArgument,
    kCancelled,
    kDeadlineExceeded,
    kNotFound,
    kAlreadyExists,
    kPermissionDenied,
    kUnauthenticated,
    kResourceExhausted,
    kFailedPrecondition,
    kAborted,
    kOutOfRange,
    kUnimplemented,
    kUnavailable,
    kInternal,
    kDataLoss,
    kUnknown,
};

struct RpcStatus {
    RpcErrorCode code{RpcErrorCode::kOk};
    std::string message;

    [[nodiscard]] bool ok() const noexcept {
        return code == RpcErrorCode::kOk;
    }

    static RpcStatus Ok() {
        return {};
    }
};

struct RpcCallOptions {
    std::string request_id;
    std::string trace_id;
    std::string caller_service;
    std::string caller_instance;

    // This is the remaining end-to-end request budget when the RPC starts.
    // It is intentionally not a fresh per-RPC timeout.
    std::chrono::milliseconds remaining_timeout{0};
};

template <typename T>
struct RpcResult {
    RpcStatus status;
    std::optional<T> value;

    [[nodiscard]] bool ok() const noexcept {
        return status.ok() && value.has_value();
    }

    static RpcResult Success(T result) {
        RpcResult output;
        output.status = RpcStatus::Ok();
        output.value = std::move(result);
        return output;
    }

    static RpcResult Failure(
        RpcErrorCode code,
        std::string message
    ) {
        RpcResult output;
        output.status.code = code;
        output.status.message = std::move(message);
        return output;
    }
};

}  // namespace tinyimx::rpc
