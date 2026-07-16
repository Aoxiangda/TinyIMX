#include "common/net/TcpServer.h"

#include "common/logging/LogMacros.h"
#include "common/net/Channel.h"
#include "common/net/EventLoop.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <unistd.h>
#include <utility>

namespace tinyimx {

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listen_address,
                     std::string name)
    : loop_(loop),
      name_(std::move(name)),
      listen_address_(listen_address) {}

TcpServer::~TcpServer() {
    Stop();
}

bool TcpServer::Start() {
    if (started_.exchange(true)) {
        LOG_WARN("tcp server already started"
                 << ", name=" << name_);
        return true;
    }

    if (loop_ == nullptr || !loop_->IsValid()) {
        LOG_ERROR("tcp server start failed: invalid event loop"
                  << ", name=" << name_);
        started_.store(false);
        return false;
    }

    if (!listen_address_.IsValid()) {
        LOG_ERROR("tcp server start failed: invalid listen address"
                  << ", name=" << name_);
        started_.store(false);
        return false;
    }

    if (!listen_socket_.CreateTcp()) {
        started_.store(false);
        return false;
    }

    if (!listen_socket_.SetReuseAddr(true)) {
        started_.store(false);
        return false;
    }

    listen_socket_.SetReusePort(true);

    if (!listen_socket_.Bind(listen_address_)) {
        started_.store(false);
        return false;
    }

    if (!listen_socket_.Listen(128)) {
        started_.store(false);
        return false;
    }

    accept_channel_ =
        std::make_unique<Channel>(loop_, listen_socket_.Fd());

    accept_channel_->SetReadCallback([this]() {
        HandleAccept();
    });

    accept_channel_->SetErrorCallback([this]() {
        LOG_ERROR("tcp server accept channel error"
                  << ", name=" << name_
                  << ", listen=" << listen_address_.ToString());
    });

    accept_channel_->EnableReading();

    LOG_INFO("tcp server started"
             << ", name=" << name_
             << ", listen=" << listen_address_.ToString()
             << ", fd=" << listen_socket_.Fd());

    return true;
}

void TcpServer::Stop() {
    if (!started_.exchange(false)) {
        return;
    }

    LOG_INFO("tcp server stopping"
             << ", name=" << name_
             << ", connection_count=" << connections_.size());

    if (accept_channel_) {
        accept_channel_->DisableAll();

        if (loop_ != nullptr) {
            loop_->RemoveChannel(accept_channel_.get());
        }

        accept_channel_.reset();
    }

    for (auto& item : connections_) {
        const auto& connection = item.second;
        if (connection) {
            connection->ConnectDestroyed();
        }
    }

    connections_.clear();

    listen_socket_.Close();

    LOG_INFO("tcp server stopped"
             << ", name=" << name_);
}

void TcpServer::SetConnectionCallback(ConnectionCallback callback) {
    connection_callback_ = std::move(callback);
}

void TcpServer::SetMessageCallback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

const std::string& TcpServer::Name() const {
    return name_;
}

const InetAddress& TcpServer::ListenAddress() const {
    return listen_address_;
}

std::size_t TcpServer::ConnectionCount() const {
    return connections_.size();
}

bool TcpServer::IsStarted() const {
    return started_.load();
}

void TcpServer::HandleAccept() {
    while (true) {
        InetAddress peer_address;

        const int conn_fd = listen_socket_.Accept(&peer_address);
        if (conn_fd < 0) {
            break;
        }

        const std::string connection_name =
            MakeConnectionName(peer_address);

        auto connection = std::make_shared<TcpConnection>(
            loop_,
            connection_name,
            conn_fd,
            listen_address_,
            peer_address
        );

        connection->SetConnectionCallback(connection_callback_);
        connection->SetMessageCallback(message_callback_);

        connection->SetCloseCallback([this](
            const TcpConnectionPtr& closed_connection
        ) {
            RemoveConnection(closed_connection);
        });

        connections_[connection_name] = connection;

        LOG_INFO("tcp server accepted new connection"
                 << ", server=" << name_
                 << ", connection=" << connection_name
                 << ", peer=" << peer_address.ToString()
                 << ", connection_count=" << connections_.size());

        connection->ConnectEstablished();
    }
}

void TcpServer::RemoveConnection(const TcpConnectionPtr& connection) {
    if (!connection) {
        return;
    }

    const std::string connection_name = connection->Name();

    auto iter = connections_.find(connection_name);
    if (iter == connections_.end()) {
        LOG_WARN("tcp server remove connection ignored: not found"
                 << ", server=" << name_
                 << ", connection=" << connection_name);
        return;
    }

    LOG_INFO("tcp server removing connection"
             << ", server=" << name_
             << ", connection=" << connection_name
             << ", peer=" << connection->PeerAddress().ToString());

    connection->ConnectDestroyed();
    connections_.erase(iter);

    LOG_INFO("tcp server connection removed"
             << ", server=" << name_
             << ", connection_count=" << connections_.size());
}

std::string TcpServer::MakeConnectionName(
    const InetAddress& peer_address
) {
    const std::uint64_t connection_id = ++next_connection_id_;

    return name_ + "-conn-" +
           std::to_string(connection_id) + "-" +
           peer_address.ToString();
}

}  // namespace tinyimx