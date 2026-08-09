#include "common/net/TcpServer.h"

#include "common/logging/LogMacros.h"
#include "common/net/Channel.h"
#include "common/net/EventLoop.h"
#include "common/net/EventLoopThreadPool.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace tinyimx {

TcpServer::TcpServer(
    EventLoop* loop,
    const InetAddress& listen_address,
    std::string name,
    std::size_t io_thread_count
)
    : loop_(loop),
      name_(std::move(name)),
      listen_address_(listen_address),
      io_thread_count_(io_thread_count) {
}

TcpServer::~TcpServer() {
    Stop();
}

bool TcpServer::Start() {
    if (started_.exchange(true)) {
        LOG_WARN(
            "tcp server already started"
            << ", name=" << name_
        );

        return true;
    }

    if (loop_ == nullptr ||
        !loop_->IsValid()) {
        LOG_ERROR(
            "tcp server start failed: "
            "invalid event loop"
            << ", name=" << name_
        );

        started_.store(false);
        return false;
    }

    if (!loop_->IsInLoopThread()) {
        LOG_ERROR(
            "tcp server start failed: "
            "not in base loop thread"
            << ", name=" << name_
        );

        started_.store(false);
        return false;
    }

    if (!listen_address_.IsValid()) {
        LOG_ERROR(
            "tcp server start failed: "
            "invalid listen address"
            << ", name=" << name_
        );

        started_.store(false);
        return false;
    }

    if (!listen_socket_.CreateTcp()) {
        started_.store(false);
        return false;
    }

    if (!listen_socket_.SetReuseAddr(true)) {
        listen_socket_.Close();
        started_.store(false);
        return false;
    }

    listen_socket_.SetReusePort(true);

    if (!listen_socket_.Bind(
            listen_address_
        )) {
        listen_socket_.Close();
        started_.store(false);
        return false;
    }

    if (!listen_socket_.Listen(128)) {
        listen_socket_.Close();
        started_.store(false);
        return false;
    }

    thread_pool_ =
        std::make_unique<
            EventLoopThreadPool
        >(
            loop_,
            io_thread_count_
        );

    if (!thread_pool_->Start()) {
        LOG_ERROR(
            "tcp server start failed: "
            "event loop thread pool "
            "start failed"
            << ", name=" << name_
            << ", io_thread_count="
            << io_thread_count_
        );

        thread_pool_.reset();
        listen_socket_.Close();
        started_.store(false);

        return false;
    }

    accept_channel_ =
        std::make_unique<Channel>(
            loop_,
            listen_socket_.Fd()
        );

    accept_channel_->SetReadCallback(
        [this]() {
            HandleAccept();
        }
    );

    accept_channel_->SetErrorCallback(
        [this]() {
            LOG_ERROR(
                "tcp server accept "
                "channel error"
                << ", name=" << name_
                << ", listen="
                << listen_address_.ToString()
            );
        }
    );

    accept_channel_->EnableReading();

    if (idle_timeout_ >
    std::chrono::milliseconds::zero()) {
    idle_check_timer_id_ =
        loop_->RunEvery(
            idle_check_interval_,
            [this]() {
                CheckIdleConnections();
            }
        );

    if (!idle_check_timer_id_.IsValid()) {
        LOG_ERROR(
            "tcp server start failed: "
            "idle timer creation failed"
            << ", name=" << name_
        );

        accept_channel_->DisableAll();

        loop_->RemoveChannel(
            accept_channel_.get()
        );

        accept_channel_.reset();

        thread_pool_.reset();

        listen_socket_.Close();

        started_.store(false);

        return false;
    }
}

    LOG_INFO(
        "tcp server started"
        << ", name=" << name_
        << ", listen="
        << listen_address_.ToString()
        << ", fd="
        << listen_socket_.Fd()
        << ", io_thread_count="
        << io_thread_count_
        << ", idle_timeout_ms="
        << idle_timeout_.count()
        << ", idle_check_interval_ms="
        << idle_check_interval_.count()
    );

    return true;
}

void TcpServer::Stop() {
    if (!started_.load()) {
        return;
    }

    if (loop_ == nullptr ||
        !loop_->IsInLoopThread()) {
        LOG_ERROR(
            "tcp server stop ignored: "
            "not in base loop thread"
            << ", name=" << name_
        );

        return;
    }

    if (!started_.exchange(false)) {
        return;
    }

    if (idle_check_timer_id_.IsValid()) {
        loop_->Cancel(
            idle_check_timer_id_
        );

        idle_check_timer_id_ = {};
    }

    LOG_INFO(
        "tcp server stopping"
        << ", name=" << name_
        << ", connection_count="
        << connections_.size()
    );

    if (accept_channel_) {
        accept_channel_->DisableAll();

        loop_->RemoveChannel(
            accept_channel_.get()
        );

        accept_channel_.reset();
    }

    std::vector<TcpConnectionPtr>
        connections;

    connections.reserve(
        connections_.size()
    );

    for (auto& item : connections_) {
        if (item.second) {
            connections.push_back(
                item.second
            );
        }
    }

    connections_.clear();

    connection_count_.store(
        0,
        std::memory_order_relaxed
    );

    std::vector<TcpConnectionPtr>
        remote_connections;

    remote_connections.reserve(
        connections.size()
    );

    for (const auto& connection :
         connections) {
        if (!connection) {
            continue;
        }

        EventLoop* io_loop =
            connection->GetLoop();

        if (io_loop == nullptr) {
            continue;
        }

        if (io_loop->IsInLoopThread()) {
            connection->ConnectDestroyed();
            continue;
        }

        remote_connections.push_back(
            connection
        );
    }

    std::mutex cleanup_mutex;
    std::condition_variable
        cleanup_condition;

    std::size_t pending_cleanup =
        remote_connections.size();

    for (const auto& connection :
         remote_connections) {
        EventLoop* io_loop =
            connection->GetLoop();

        if (io_loop == nullptr) {
            {
                std::lock_guard<std::mutex>
                    lock(cleanup_mutex);

                --pending_cleanup;
            }

            cleanup_condition.notify_one();
            continue;
        }

        io_loop->QueueInLoop(
            [
                connection,
                &cleanup_mutex,
                &cleanup_condition,
                &pending_cleanup
            ]() {
                connection->
                    ConnectDestroyed();

                {
                    std::lock_guard<std::mutex>
                        lock(cleanup_mutex);

                    --pending_cleanup;
                }

                cleanup_condition.notify_one();
            }
        );
    }

    if (!remote_connections.empty()) {
        std::unique_lock<std::mutex>
            lock(cleanup_mutex);

        cleanup_condition.wait(
            lock,
            [&pending_cleanup]() {
                return pending_cleanup == 0;
            }
        );
    }

    thread_pool_.reset();

    listen_socket_.Close();

    LOG_INFO(
        "tcp server stopping"
        << ", name=" << name_
        << ", connection_count="
        << ConnectionCount()
    );
}

void TcpServer::SetConnectionCallback(
    ConnectionCallback callback
) {
    connection_callback_ =
        std::move(callback);
}

void TcpServer::SetMessageCallback(
    MessageCallback callback
) {
    message_callback_ =
        std::move(callback);
}

bool TcpServer::SetIdleTimeout(
    std::chrono::milliseconds idle_timeout,
    std::chrono::milliseconds check_interval
) {
    if (started_.load(
            std::memory_order_relaxed
        )) {
        LOG_ERROR(
            "set idle timeout failed: "
            "server already started"
            << ", name=" << name_
        );

        return false;
    }

    if (idle_timeout ==
        std::chrono::milliseconds::zero()) {
        idle_timeout_ =
            std::chrono::milliseconds::zero();

        idle_check_interval_ =
            std::chrono::milliseconds::zero();

        return true;
    }

    if (idle_timeout <
            std::chrono::milliseconds::zero() ||
        check_interval <=
            std::chrono::milliseconds::zero() ||
        check_interval >
            idle_timeout) {
        LOG_ERROR(
            "set idle timeout failed: "
            "invalid duration"
            << ", name=" << name_
            << ", idle_timeout_ms="
            << idle_timeout.count()
            << ", check_interval_ms="
            << check_interval.count()
        );

        return false;
    }

    idle_timeout_ =
        idle_timeout;

    idle_check_interval_ =
        check_interval;

    return true;
}

const std::string&
TcpServer::Name() const {
    return name_;
}

const InetAddress&
TcpServer::ListenAddress() const {
    return listen_address_;
}

std::size_t
TcpServer::ConnectionCount() const {
    return connection_count_.load(
        std::memory_order_relaxed
    );
}

bool TcpServer::IsStarted() const {
    return started_.load();
}

void TcpServer::HandleAccept() {
    if (loop_ == nullptr ||
        !loop_->IsInLoopThread()) {
        LOG_ERROR(
            "tcp server handle accept "
            "called outside base loop"
            << ", name=" << name_
        );

        return;
    }

    while (true) {
        InetAddress peer_address;

        const int conn_fd =
            listen_socket_.Accept(
                &peer_address
            );

        if (conn_fd < 0) {
            break;
        }

        EventLoop* io_loop =
            loop_;

        if (thread_pool_) {
            io_loop =
                thread_pool_->
                    GetNextLoop();
        }

        if (io_loop == nullptr) {
            LOG_ERROR(
                "tcp server accept failed: "
                "no available io loop"
                << ", server=" << name_
                << ", peer="
                << peer_address.ToString()
            );

            ::close(conn_fd);
            continue;
        }

        const std::string
            connection_name =
                MakeConnectionName(
                    peer_address
                );

        auto connection =
            std::make_shared<
                TcpConnection
            >(
                io_loop,
                connection_name,
                conn_fd,
                listen_address_,
                peer_address
            );

        connection->
            SetConnectionCallback(
                connection_callback_
            );

        connection->
            SetMessageCallback(
                message_callback_
            );

        connection->SetCloseCallback(
            [this](
                const TcpConnectionPtr&
                    closed_connection
            ) {
                RemoveConnection(
                    closed_connection
                );
            }
        );

        connections_[
            connection_name
        ] = connection;

        connection_count_.store(
            connections_.size(),
            std::memory_order_relaxed
        );

        LOG_INFO(
            "tcp server accepted "
            "new connection"
            << ", server=" << name_
            << ", connection="
            << connection_name
            << ", peer="
            << peer_address.ToString()
            << ", reactor="
            << (
                io_loop == loop_
                    ? "base"
                    : "sub"
            )
            << ", connection_count="
            << connections_.size()
        );

        io_loop->RunInLoop(
            [connection]() {
                connection->
                    ConnectEstablished();
            }
        );
    }
}

void TcpServer::RemoveConnection(
    const TcpConnectionPtr&
        connection
) {
    if (!connection) {
        return;
    }

    if (!started_.load()) {
        return;
    }

    if (loop_ == nullptr) {
        return;
    }

    loop_->RunInLoop(
        [this, connection]() {
            RemoveConnectionInLoop(
                connection
            );
        }
    );
}

void TcpServer::RemoveConnectionInLoop(
    const TcpConnectionPtr&
        connection
) {
    if (!connection) {
        return;
    }

    if (loop_ == nullptr ||
        !loop_->IsInLoopThread()) {
        LOG_ERROR(
            "tcp server remove connection "
            "called outside base loop"
            << ", server=" << name_
            << ", connection="
            << connection->Name()
        );

        return;
    }

    const std::string connection_name =
        connection->Name();

    auto iter =
        connections_.find(
            connection_name
        );

    if (iter ==
        connections_.end()) {
        LOG_WARN(
            "tcp server remove connection "
            "ignored: not found"
            << ", server=" << name_
            << ", connection="
            << connection_name
        );

        return;
    }

    LOG_INFO(
        "tcp server removing connection"
        << ", server=" << name_
        << ", connection="
        << connection_name
        << ", peer="
        << connection->
            PeerAddress().ToString()
    );

    connections_.erase(iter);

    connection_count_.store(
        connections_.size(),
        std::memory_order_relaxed
    );

    EventLoop* io_loop =
        connection->GetLoop();

    if (io_loop == nullptr) {
        LOG_ERROR(
            "tcp server remove connection "
            "failed: invalid io loop"
            << ", server=" << name_
            << ", connection="
            << connection_name
        );

        return;
    }

    io_loop->QueueInLoop(
        [connection]() {
            connection->
                ConnectDestroyed();
        }
    );

    LOG_INFO(
        "tcp server connection removed"
        << ", server=" << name_
        << ", connection_count="
        << ConnectionCount()
    );

}

void TcpServer::CheckIdleConnections() {
    if (loop_ == nullptr ||
        !loop_->IsInLoopThread()) {
        LOG_ERROR(
            "idle connection check "
            "called outside base loop"
            << ", server=" << name_
        );

        return;
    }

    if (!started_.load(
            std::memory_order_relaxed
        ) ||
        idle_timeout_ <=
            std::chrono::milliseconds::zero()) {
        return;
    }

    const auto now =
        TcpConnection::Clock::now();

    std::vector<TcpConnectionPtr>
        idle_connections;

    idle_connections.reserve(
        connections_.size()
    );

    for (const auto& item :
         connections_) {
        const TcpConnectionPtr&
            connection =
                item.second;

        if (!connection ||
            !connection->IsConnected()) {
            continue;
        }

        const auto last_read_time =
            connection->
                LastReadTime();

        const auto idle_duration =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                now -
                last_read_time
            );

        if (idle_duration >=
            idle_timeout_) {
            idle_connections.push_back(
                connection
            );
        }
    }

    for (const auto& connection :
         idle_connections) {
        if (!connection) {
            continue;
        }

        const auto idle_duration =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                now -
                connection->
                    LastReadTime()
            );

        LOG_WARN(
            "tcp connection idle timeout"
            << ", server=" << name_
            << ", connection="
            << connection->Name()
            << ", peer="
            << connection->
                PeerAddress().ToString()
            << ", idle_ms="
            << idle_duration.count()
            << ", timeout_ms="
            << idle_timeout_.count()
        );

        connection->ForceClose();
    }
}

std::string
TcpServer::MakeConnectionName(
    const InetAddress& peer_address
) {
    const std::uint64_t
        connection_id =
            ++next_connection_id_;

    return name_
        + "-conn-"
        + std::to_string(
            connection_id
        )
        + "-"
        + peer_address.ToString();
}

}  // namespace tinyimx