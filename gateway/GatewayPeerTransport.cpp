#include "gateway/GatewayPeerTransport.h"

#include "common/logging/LogMacros.h"
#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpClient.h"
#include "common/net/TcpConnection.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace tinyimx {


GatewayPeerTransport::
GatewayPeerTransport(
    EventLoop* loop,
    GatewayPeerTransportOptions options
)
    : loop_(loop),
      options_(std::move(options)),
      codec_(options_.max_body_size) {
}


GatewayPeerTransport::
~GatewayPeerTransport() {
    if (
        running_.load(
            std::memory_order_acquire
        )
    ) {
        LOG_WARN(
            "gateway peer transport "
            "destroyed while running"
            << ", remote_gateway="
            << options_.remote_gateway_id
        );
    }
}


bool GatewayPeerTransport::Start() {
    if (loop_ == nullptr ||
        !loop_->IsValid()) {
        LOG_ERROR(
            "gateway peer transport "
            "start failed: invalid loop"
        );

        return false;
    }

    std::string error_message;

    if (!ValidateOptions(
            &error_message
        )) {
        LOG_ERROR(
            "gateway peer transport "
            "start failed"
            << ", error="
            << error_message
        );

        return false;
    }

    bool expected = false;

    if (
        !running_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel
        )
    ) {
        return true;
    }

    const auto self =
        weak_from_this().lock();

    if (!self) {
        running_.store(
            false,
            std::memory_order_release
        );

        LOG_ERROR(
            "gateway peer transport must "
            "be owned by shared_ptr"
        );

        return false;
    }

    loop_->RunInLoop(
        [self]() {
            self->StartInLoop();
        }
    );

    return true;
}


void GatewayPeerTransport::Stop() {
    if (
        !running_.exchange(
            false,
            std::memory_order_acq_rel
        )
    ) {
        return;
    }

    const auto self =
        weak_from_this().lock();

    if (!self || loop_ == nullptr) {
        return;
    }

    loop_->RunInLoop(
        [self]() {
            self->StopInLoop();
        }
    );
}


bool GatewayPeerTransport::
ForwardChat(
    std::uint64_t message_id,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    std::string message_body,
    ForwardChatCallback callback
) {
    if (
        message_id == 0 ||
        from_user_id == 0 ||
        to_user_id == 0 ||
        from_user_id == to_user_id ||
        message_body.empty()
    ) {
        return false;
    }

    if (
        !running_.load(
            std::memory_order_acquire
        )
    ) {
        return false;
    }

    const auto self =
        weak_from_this().lock();

    if (!self || loop_ == nullptr) {
        return false;
    }

    loop_->RunInLoop(
        [
            self,
            message_id,
            from_user_id,
            to_user_id,
            message_body =
                std::move(message_body),
            callback =
                std::move(callback)
        ]() mutable {
            self->ForwardChatInLoop(
                message_id,
                from_user_id,
                to_user_id,
                std::move(message_body),
                std::move(callback)
            );
        }
    );

    return true;
}


bool GatewayPeerTransport::
IsRunning() const noexcept {
    return running_.load(
        std::memory_order_acquire
    );
}


bool GatewayPeerTransport::
IsConnected() const noexcept {
    return connected_.load(
        std::memory_order_acquire
    );
}


std::size_t
GatewayPeerTransport::
PendingCount() const noexcept {
    return pending_count_.load(
        std::memory_order_acquire
    );
}


void GatewayPeerTransport::
SetConnectionStateCallback(
    ConnectionStateCallback callback
) {
    connection_state_callback_ =
        std::move(callback);
}


const std::string&
GatewayPeerTransport::
RemoteGatewayId() const noexcept {
    return options_.remote_gateway_id;
}


void GatewayPeerTransport::
StartInLoop() {
    if (
        !running_.load(
            std::memory_order_acquire
        )
    ) {
        return;
    }

    if (!tcp_client_) {
        const InetAddress
            remote_address(
                options_.remote_host,
                options_.remote_port
            );

        tcp_client_ =
            std::make_unique<TcpClient>(
                loop_,
                remote_address,
                "gateway-peer-" +
                    options_.remote_gateway_id,
                options_.connect_timeout
            );

        const std::weak_ptr<
            GatewayPeerTransport
        > weak_self =
            weak_from_this();

        tcp_client_->
            SetConnectionCallback(
                [weak_self](
                    const TcpConnectionPtr&
                        connection
                ) {
                    const auto self =
                        weak_self.lock();

                    if (self) {
                        self->HandleConnection(
                            connection
                        );
                    }
                }
            );

        tcp_client_->
            SetMessageCallback(
                [weak_self](
                    const TcpConnectionPtr&
                        connection,
                    Buffer* buffer
                ) {
                    const auto self =
                        weak_self.lock();

                    if (self) {
                        self->HandleMessage(
                            connection,
                            buffer
                        );
                    }
                }
            );

        tcp_client_->
            SetConnectErrorCallback(
                [weak_self](
                    int error_number
                ) {
                    const auto self =
                        weak_self.lock();

                    if (self) {
                        self->
                            HandleConnectError(
                                error_number
                            );
                    }
                }
            );
    }

    if (!tcp_client_->Connect()) {
        running_.store(
            false,
            std::memory_order_release
        );

        LOG_ERROR(
            "gateway peer transport "
            "connect submission failed"
            << ", remote_gateway="
            << options_.remote_gateway_id
        );

        return;
    }

    LOG_INFO(
        "gateway peer transport started"
        << ", local_gateway="
        << options_.local_gateway_id
        << ", remote_gateway="
        << options_.remote_gateway_id
        << ", remote="
        << options_.remote_host
        << ':'
        << options_.remote_port
    );
}


void GatewayPeerTransport::
StopInLoop() {
    FailAllPending(
        GatewayPeerTransportStatus::
            kStopped,
        "gateway peer transport stopped"
    );
    FailQueuedForwards(
        GatewayPeerTransportStatus::
            kStopped,
        "gateway peer transport stopped"
    );
    connected_.store(
        false,
        std::memory_order_release
    );

    if (tcp_client_) {
        tcp_client_->Stop();
    }

    LOG_INFO(
        "gateway peer transport stopped"
        << ", remote_gateway="
        << options_.remote_gateway_id
    );
}


void GatewayPeerTransport::HandleConnection(
    const TcpConnectionPtr& connection
) {
    const bool connected =
        connection &&
        connection->IsConnected();

    connected_.store(
        connected,
        std::memory_order_release
    );

    if (!connected) {
        running_.store(
            false,
            std::memory_order_release
        );

        FailAllPending(
            GatewayPeerTransportStatus::
                kDisconnected,
            "gateway peer connection closed"
        );

        FailQueuedForwards(
            GatewayPeerTransportStatus::
                kDisconnected,
            "gateway peer connection closed"
        );
    }

    LOG_INFO(
        "gateway peer connection state changed"
        << ", remote_gateway="
        << options_.remote_gateway_id
        << ", connected="
        << connected
    );

    if (connection_state_callback_) {
        connection_state_callback_(
            connected
        );
    }

    if (connected) {
        FlushQueuedForwards();
    }
}


void GatewayPeerTransport::
HandleMessage(
    const TcpConnectionPtr& connection,
    Buffer* buffer
) {
    if (buffer == nullptr) {
        return;
    }

    const DecodeResult decode_result =
        codec_.Decode(buffer);

    if (
        decode_result.status ==
        DecodeStatus::kNeedMoreData
    ) {
        return;
    }

    if (
        decode_result.status !=
        DecodeStatus::kOk
    ) {
        LOG_WARN(
            "gateway peer protocol "
            "decode failed"
            << ", remote_gateway="
            << options_.remote_gateway_id
            << ", status="
            << DecodeStatusToString(
                decode_result.status
            )
            << ", error="
            << decode_result.error_message
        );

        FailAllPending(
            GatewayPeerTransportStatus::
                kProtocolError,
            decode_result.error_message
        );

        if (connection) {
            connection->ForceClose();
        }

        return;
    }

    for (
        const Packet& packet :
        decode_result.packets
    ) {
        if (
            packet.type ==
            MessageType::
                kGatewayForwardChatResponse
        ) {
            HandleForwardChatResponse(
                packet
            );

            continue;
        }

        LOG_WARN(
            "gateway peer transport "
            "received unexpected packet"
            << ", remote_gateway="
            << options_.remote_gateway_id
            << ", type="
            << MessageTypeToString(
                packet.type
            )
            << ", seq="
            << packet.seq
        );
    }
}


void GatewayPeerTransport::
HandleConnectError(
    int error_number
) {
    connected_.store(
        false,
        std::memory_order_release
    );
    running_.store(
        false,
        std::memory_order_release
    );

    FailQueuedForwards(
        GatewayPeerTransportStatus::
            kNotConnected,
        std::string(
            "gateway peer connect failed: "
        ) +
            std::strerror(error_number)
    );
    LOG_WARN(
        "gateway peer transport "
        "connect failed"
        << ", remote_gateway="
        << options_.remote_gateway_id
        << ", remote="
        << options_.remote_host
        << ':'
        << options_.remote_port
        << ", error="
        << std::strerror(
            error_number
        )
    );

    if (connection_state_callback_) {
        connection_state_callback_(
            false
        );
    }
}


void GatewayPeerTransport::
ForwardChatInLoop(
    std::uint64_t message_id,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    std::string message_body,
    ForwardChatCallback callback
) {
    if (
        !running_.load(
            std::memory_order_acquire
        )
    ) {
        if (callback) {
            GatewayPeerTransportResult result;

            result.status =
                GatewayPeerTransportStatus::
                    kNotRunning;

            result.error_message =
                "gateway peer transport "
                "is not running";

            callback(
                std::move(result)
            );
        }

        return;
    }

    if (
        !connected_.load(
            std::memory_order_acquire
        ) ||
        !tcp_client_
    ) {
        QueuedForwardChat queued;

        queued.message_id =
            message_id;

        queued.from_user_id =
            from_user_id;

        queued.to_user_id =
            to_user_id;

        queued.message_body =
            std::move(message_body);

        queued.callback =
            std::move(callback);

        queued_forwards_.push_back(
            std::move(queued)
        );

        LOG_INFO(
            "gateway peer request queued "
            "while connecting"
            << ", remote_gateway="
            << options_.remote_gateway_id
            << ", queued="
            << queued_forwards_.size()
        );

        return;
    }

    const TcpConnectionPtr connection =
        tcp_client_->Connection();

    if (
        !connection ||
        !connection->IsConnected()
    ) {
        QueuedForwardChat queued;

        queued.message_id =
            message_id;

        queued.from_user_id =
            from_user_id;

        queued.to_user_id =
            to_user_id;

        queued.message_body =
            std::move(message_body);

        queued.callback =
            std::move(callback);

        queued_forwards_.push_back(
            std::move(queued)
        );

        return;
    }

    GatewayForwardChatRequest request;

    request.message_id = message_id;

    request.source_gateway_id =
        options_.local_gateway_id;

    request.source_lease_token =
        options_.local_lease_token;

    request.from_user_id =
        from_user_id;

    request.to_user_id =
        to_user_id;

    request.message_body =
        std::move(message_body);

    std::string request_body;
    std::string error_message;

    if (
        !SerializeGatewayForwardChatRequest(
            request,
            &request_body,
            &error_message
        )
    ) {
        if (callback) {
            GatewayPeerTransportResult result;

            result.status =
                GatewayPeerTransportStatus::
                    kInvalidArgument;

            result.error_message =
                error_message;

            callback(
                std::move(result)
            );
        }

        return;
    }

    const std::uint32_t sequence =
        NextSequence();

    if (sequence == 0) {
        if (callback) {
            GatewayPeerTransportResult result;

            result.status =
                GatewayPeerTransportStatus::
                    kProtocolError;

            result.error_message =
                "unable to allocate "
                "gateway peer sequence";

            callback(
                std::move(result)
            );
        }

        return;
    }

    Packet packet;

    packet.type =
        MessageType::
            kGatewayForwardChatRequest;

    packet.seq = sequence;

    packet.body =
        std::move(request_body);

    Buffer output;

    if (
        !codec_.Encode(
            packet,
            &output,
            &error_message
        )
    ) {
        if (callback) {
            GatewayPeerTransportResult result;

            result.status =
                GatewayPeerTransportStatus::
                    kEncodeError;

            result.error_message =
                error_message;

            callback(
                std::move(result)
            );
        }

        return;
    }

    const std::weak_ptr<
        GatewayPeerTransport
    > weak_self =
        weak_from_this();

    const TimerId timeout_timer =
        loop_->RunAfter(
            options_.request_timeout,
            [
                weak_self,
                sequence
            ]() {
                const auto self =
                    weak_self.lock();

                if (self) {
                    self->
                        HandleRequestTimeout(
                            sequence
                        );
                }
            }
        );

    PendingRequest pending;

    pending.callback =
        std::move(callback);

    pending.timeout_timer =
        timeout_timer;

    pending_requests_.emplace(
        sequence,
        std::move(pending)
    );

    pending_count_.store(
        pending_requests_.size(),
        std::memory_order_release
    );

    connection->Send(
        output.RetrieveAllAsString()
    );

    LOG_INFO(
        "gateway peer request sent"
        << ", remote_gateway="
        << options_.remote_gateway_id
        << ", seq="
        << sequence
        << ", message_id="
        << message_id
        << ", from="
        << from_user_id
        << ", to="
        << to_user_id
        << ", pending="
        << pending_requests_.size()
    );
}


void GatewayPeerTransport::
HandleForwardChatResponse(
    const Packet& packet
) {
    const auto iterator =
        pending_requests_.find(
            packet.seq
        );

    if (
        iterator ==
        pending_requests_.end()
    ) {
        LOG_WARN(
            "gateway peer received "
            "unknown or expired response"
            << ", remote_gateway="
            << options_.remote_gateway_id
            << ", seq="
            << packet.seq
        );

        return;
    }

    PendingRequest pending =
        std::move(
            iterator->second
        );

    pending_requests_.erase(
        iterator
    );

    pending_count_.store(
        pending_requests_.size(),
        std::memory_order_release
    );

    if (
        pending.timeout_timer.IsValid()
    ) {
        loop_->Cancel(
            pending.timeout_timer
        );
    }

    GatewayForwardChatResponse response;

    std::string error_message;

    if (
        !DeserializeGatewayForwardChatResponse(
            packet.body,
            &response,
            &error_message
        )
    ) {
        if (pending.callback) {
            GatewayPeerTransportResult result;

            result.status =
                GatewayPeerTransportStatus::
                    kProtocolError;

            result.error_message =
                error_message;

            pending.callback(
                std::move(result)
            );
        }

        return;
    }

    if (pending.callback) {
        GatewayPeerTransportResult result;

        result.status =
            GatewayPeerTransportStatus::
                kOk;

        result.response =
            std::move(response);

        pending.callback(
            std::move(result)
        );
    }
}


void GatewayPeerTransport::HandleRequestTimeout(
    std::uint32_t sequence
) {
    const auto iterator =
        pending_requests_.find(
            sequence
        );

    if (
        iterator ==
        pending_requests_.end()
    ) {
        return;
    }

    ForwardChatCallback callback =
        std::move(
            iterator->second.callback
        );

    pending_requests_.erase(
        iterator
    );

    pending_count_.store(
        pending_requests_.size(),
        std::memory_order_release
    );

    LOG_WARN(
        "gateway peer request timeout"
        << ", remote_gateway="
        << options_.remote_gateway_id
        << ", seq="
        << sequence
    );

    if (callback) {
        GatewayPeerTransportResult result;

        result.status =
            GatewayPeerTransportStatus::
                kRequestTimeout;

        result.error_message =
            "gateway peer request timeout";

        callback(
            std::move(result)
        );
    }
}


void GatewayPeerTransport::
FailAllPending(
    GatewayPeerTransportStatus status,
    const std::string& error_message
) {
    if (pending_requests_.empty()) {
        pending_count_.store(
            0,
            std::memory_order_release
        );

        return;
    }

    auto pending_requests =
        std::move(
            pending_requests_
        );

    pending_requests_.clear();

    pending_count_.store(
        0,
        std::memory_order_release
    );

    for (
        auto& entry :
        pending_requests
    ) {
        PendingRequest& pending =
            entry.second;

        if (
            pending.timeout_timer.IsValid()
        ) {
            loop_->Cancel(
                pending.timeout_timer
            );
        }

        if (!pending.callback) {
            continue;
        }

        GatewayPeerTransportResult result;

        result.status =
            status;

        result.error_message =
            error_message;

        pending.callback(
            std::move(result)
        );
    }
}



void GatewayPeerTransport::
FlushQueuedForwards() {
    if (queued_forwards_.empty()) {
        return;
    }

    auto queued =
        std::move(queued_forwards_);

    queued_forwards_.clear();

    while (!queued.empty()) {
        QueuedForwardChat request =
            std::move(
                queued.front()
            );

        queued.pop_front();

        ForwardChatInLoop(
            request.message_id,
            request.from_user_id,
            request.to_user_id,
            std::move(
                request.message_body
            ),
            std::move(
                request.callback
            )
        );
    }
}


void GatewayPeerTransport::
FailQueuedForwards(
    GatewayPeerTransportStatus status,
    const std::string& error_message
) {
    auto queued =
        std::move(queued_forwards_);

    queued_forwards_.clear();

    while (!queued.empty()) {
        QueuedForwardChat request =
            std::move(
                queued.front()
            );

        queued.pop_front();

        if (!request.callback) {
            continue;
        }

        GatewayPeerTransportResult result;

        result.status =
            status;

        result.error_message =
            error_message;

        request.callback(
            std::move(result)
        );
    }
}


std::uint32_t
GatewayPeerTransport::
NextSequence() {
    constexpr std::uint64_t
        kMaxAttempts =
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::uint32_t
                >::max()
            );

    for (
        std::uint64_t attempt = 0;
        attempt < kMaxAttempts;
        ++attempt
    ) {
        std::uint32_t candidate =
            next_sequence_++;

        if (next_sequence_ == 0) {
            next_sequence_ = 1;
        }

        if (candidate == 0) {
            continue;
        }

        if (
            pending_requests_.find(
                candidate
            ) ==
            pending_requests_.end()
        ) {
            return candidate;
        }
    }

    return 0;
}


bool GatewayPeerTransport::
ValidateOptions(
    std::string* error_message
) const {
    auto set_error =
        [error_message](
            const std::string& message
        ) {
            if (error_message) {
                *error_message =
                    message;
            }
        };

    if (
        options_.local_gateway_id.
            empty()
    ) {
        set_error(
            "local_gateway_id is empty"
        );

        return false;
    }

    if (
        options_.local_lease_token.
            empty()
    ) {
        set_error(
            "local_lease_token is empty"
        );

        return false;
    }

    if (
        options_.remote_gateway_id.
            empty()
    ) {
        set_error(
            "remote_gateway_id is empty"
        );

        return false;
    }

    if (
        options_.remote_gateway_id ==
        options_.local_gateway_id
    ) {
        set_error(
            "remote gateway equals "
            "local gateway"
        );

        return false;
    }

    if (options_.remote_host.empty()) {
        set_error(
            "remote_host is empty"
        );

        return false;
    }

    if (options_.remote_port == 0) {
        set_error(
            "remote_port is zero"
        );

        return false;
    }

    const InetAddress address(
        options_.remote_host,
        options_.remote_port
    );

    if (!address.IsValid()) {
        set_error(
            "remote address is invalid"
        );

        return false;
    }

    if (
        options_.connect_timeout <=
        std::chrono::milliseconds::zero()
    ) {
        set_error(
            "connect_timeout must be positive"
        );

        return false;
    }

    if (
        options_.request_timeout <=
        std::chrono::milliseconds::zero()
    ) {
        set_error(
            "request_timeout must be positive"
        );

        return false;
    }

    if (options_.max_body_size == 0) {
        set_error(
            "max_body_size is zero"
        );

        return false;
    }

    if (error_message) {
        error_message->clear();
    }

    return true;
}


std::string
GatewayPeerTransportStatusToString(
    GatewayPeerTransportStatus status
) {
    switch (status) {
        case GatewayPeerTransportStatus::kOk:
            return "ok";

        case GatewayPeerTransportStatus::
            kInvalidArgument:
            return "invalid_argument";

        case GatewayPeerTransportStatus::
            kNotRunning:
            return "not_running";

        case GatewayPeerTransportStatus::
            kNotConnected:
            return "not_connected";

        case GatewayPeerTransportStatus::
            kEncodeError:
            return "encode_error";

        case GatewayPeerTransportStatus::
            kRequestTimeout:
            return "request_timeout";

        case GatewayPeerTransportStatus::
            kDisconnected:
            return "disconnected";

        case GatewayPeerTransportStatus::
            kProtocolError:
            return "protocol_error";

        case GatewayPeerTransportStatus::
            kStopped:
            return "stopped";
    }

    return "unknown";
}

}  // namespace tinyimx