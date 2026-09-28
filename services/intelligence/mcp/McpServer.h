#pragma once

#include "common/concurrency/ThreadPool.h"
#include "common/net/TcpServer.h"
#include "services/intelligence/mcp/HttpCodec.h"
#include "services/intelligence/mcp/McpAuth.h"
#include "services/intelligence/mcp/McpDispatcher.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>

namespace tinyimx::mcp {

struct ServerOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    std::string endpoint_path{"/mcp"};
    std::size_t io_threads{1};
    std::size_t worker_threads{4};
    std::size_t queue_capacity{128};
    std::size_t max_request_bytes{1024 * 1024};
    std::chrono::milliseconds request_timeout{5000};
    std::unordered_set<std::string> allowed_origins;
};

class Server final {
public:
    Server(EventLoop* loop, ServerOptions options,
           std::shared_ptr<const Dispatcher> dispatcher,
           std::shared_ptr<const AccessTokenVerifier> token_verifier);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool Start();
    void Stop();
    [[nodiscard]] bool IsStarted() const noexcept;

private:
    void OnConnection(const TcpConnectionPtr& connection);
    void OnMessage(const TcpConnectionPtr& connection, Buffer* buffer);
    void HandleRequest(const TcpConnectionPtr& connection, HttpRequest request);
    void ReplyAndClose(const TcpConnectionPtr& connection, int status, Json body) const;
    [[nodiscard]] bool OriginAllowed(const std::string& origin) const;

    EventLoop* loop_{nullptr};
    ServerOptions options_;
    HttpCodec http_codec_;
    std::shared_ptr<const Dispatcher> dispatcher_;
    std::shared_ptr<const AccessTokenVerifier> token_verifier_;
    ThreadPool worker_pool_;
    TcpServer server_;
    std::atomic<bool> started_{false};
};

}  // namespace tinyimx::mcp
