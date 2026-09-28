#include "services/intelligence/mcp/McpServer.h"

#include "common/logging/LogMacros.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <utility>

namespace tinyimx::mcp {
namespace {

ThreadPoolOptions MakePoolOptions(const ServerOptions& options) {
    ThreadPoolOptions pool;
    pool.name = "mcp-worker";
    pool.worker_threads = options.worker_threads;
    pool.queue_capacity = options.queue_capacity;
    pool.queue_full_policy = QueueFullPolicy::kDiscard;
    pool.enable_dynamic_resize = false;
    pool.min_threads = options.worker_threads;
    pool.max_threads = options.worker_threads;
    return pool;
}

Json SimpleError(int code, const std::string& message) {
    return MakeJsonRpcError(nullptr, code, message);
}

}  // namespace

Server::Server(EventLoop* loop, ServerOptions options,
               std::shared_ptr<const Dispatcher> dispatcher,
               std::shared_ptr<const AccessTokenVerifier> token_verifier)
    : loop_(loop),
      options_(std::move(options)),
      http_codec_(options_.max_request_bytes),
      dispatcher_(std::move(dispatcher)),
      token_verifier_(std::move(token_verifier)),
      worker_pool_(MakePoolOptions(options_)),
      server_(loop_, InetAddress(options_.host, options_.port), "tinyimx-mcp", options_.io_threads) {
    server_.SetConnectionCallback([this](const TcpConnectionPtr& c) { OnConnection(c); });
    server_.SetMessageCallback([this](const TcpConnectionPtr& c, Buffer* b) { OnMessage(c, b); });
}

Server::~Server() { Stop(); }

bool Server::Start() {
    if (started_.exchange(true)) return true;
    if (loop_ == nullptr || dispatcher_ == nullptr || token_verifier_ == nullptr) {
        started_.store(false);
        return false;
    }
    if (!worker_pool_.Start()) {
        started_.store(false);
        return false;
    }
    if (!server_.Start()) {
        worker_pool_.Shutdown(ShutdownMode::kGraceful, std::chrono::milliseconds(5000));
        started_.store(false);
        return false;
    }
    LOG_INFO("MCP server started, listen=" << options_.host << ':' << options_.port
             << ", path=" << options_.endpoint_path);
    return true;
}

void Server::Stop() {
    if (!started_.exchange(false)) return;
    server_.Stop();
    worker_pool_.Shutdown(ShutdownMode::kGraceful, std::chrono::milliseconds(5000));
    LOG_INFO("MCP server stopped");
}

bool Server::IsStarted() const noexcept { return started_.load(); }

void Server::OnConnection(const TcpConnectionPtr& connection) {
    if (connection && connection->IsConnected()) {
        connection->SetHighWaterMarkCallback([](const TcpConnectionPtr& c, std::size_t bytes) {
            LOG_WARN("MCP connection high-water mark, peer=" << c->PeerAddress().ToString() << ", buffered=" << bytes);
            c->ForceClose();
        }, 4U * 1024U * 1024U);
    }
}

void Server::OnMessage(const TcpConnectionPtr& connection, Buffer* buffer) {
    const auto decoded = http_codec_.Decode(buffer);
    switch (decoded.status) {
        case HttpDecodeStatus::kNeedMoreData:
            return;
        case HttpDecodeStatus::kPayloadTooLarge:
            ReplyAndClose(connection, 413, SimpleError(kInvalidRequest, decoded.error));
            return;
        case HttpDecodeStatus::kUnsupportedTransferEncoding:
        case HttpDecodeStatus::kBadRequest:
            ReplyAndClose(connection, 400, SimpleError(kInvalidRequest, decoded.error));
            return;
        case HttpDecodeStatus::kComplete:
            break;
    }

    HttpRequest request = decoded.request;
    const auto push = worker_pool_.TrySubmit([this, connection, request = std::move(request)]() mutable {
        HandleRequest(connection, std::move(request));
    });
    if (push != TaskPushResult::kOk) {
        ReplyAndClose(connection, 503, SimpleError(kOverloaded, "MCP worker queue overloaded"));
    }
}

bool Server::OriginAllowed(const std::string& origin) const {
    if (origin.empty()) return true;
    return options_.allowed_origins.find(origin) != options_.allowed_origins.end();
}

void Server::HandleRequest(const TcpConnectionPtr& connection, HttpRequest request) {
    if (request.target == "/health" && request.method == "GET") {
        ReplyAndClose(connection, 200, Json{{"status", "ok"}, {"service", "tinyimx-mcp"}});
        return;
    }
    if (request.target != options_.endpoint_path) {
        ReplyAndClose(connection, 404, SimpleError(kMethodNotFound, "unknown endpoint"));
        return;
    }
    if (request.method != "POST") {
        ReplyAndClose(connection, 405, SimpleError(kInvalidRequest, "MCP endpoint accepts POST only"));
        return;
    }
    const std::string content_type = request.Header("content-type");
    if (content_type.rfind("application/json", 0) != 0) {
        ReplyAndClose(connection, 415, SimpleError(kInvalidRequest, "Content-Type must be application/json"));
        return;
    }
    if (!OriginAllowed(request.Header("origin"))) {
        ReplyAndClose(connection, 403, SimpleError(kForbidden, "Origin is not allowed"));
        return;
    }

    if (request.Header("mcp-protocol-version").empty() ||
        request.Header("mcp-method").empty()) {
        ReplyAndClose(connection, 400, MakeJsonRpcError(nullptr, kHeaderMismatch,
            "MCP-Protocol-Version and Mcp-Method are required"));
        return;
    }

    const auto auth = token_verifier_->Verify(request.Header("authorization"));
    if (!auth.ok) {
        ReplyAndClose(connection, 401, SimpleError(kUnauthorized, auth.error));
        return;
    }

    Json body;
    try {
        body = Json::parse(request.body);
    } catch (const Json::parse_error& e) {
        ReplyAndClose(connection, 400, MakeJsonRpcError(nullptr, kParseError, e.what()));
        return;
    }

    RequestContext context;
    context.principal = auth.principal;
    context.request_id = request.Header("x-request-id");
    context.trace_id = request.Header("traceparent");
    context.deadline = std::chrono::steady_clock::now() + options_.request_timeout;

    const Json meta = body.value("params", Json::object()).value("_meta", Json::object());
    if (meta.is_object()) {
        if (meta.contains("io.modelcontextprotocol/clientInfo") && meta.at("io.modelcontextprotocol/clientInfo").is_object()) {
            const auto& info = meta.at("io.modelcontextprotocol/clientInfo");
            context.client_name = info.value("name", "");
            context.client_version = info.value("version", "");
        }
    }

    DispatchMetadata metadata;
    metadata.protocol_version = request.Header("mcp-protocol-version");
    metadata.method_header = request.Header("mcp-method");
    metadata.name_header = request.Header("mcp-name");
    const DispatchResult result = dispatcher_->Dispatch(context, metadata, body);
    ReplyAndClose(connection, result.http_status, result.body);
}

void Server::ReplyAndClose(const TcpConnectionPtr& connection, int status, Json body) const {
    if (!connection) return;
    const std::string response = HttpCodec::EncodeJsonResponse(status, body.dump());
    // TcpConnection::Send/Shutdown are thread-safe and preserve queue order when
    // invoked from the same worker thread. Avoid mutating connection callbacks
    // outside its EventLoop thread.
    connection->Send(response);
    connection->Shutdown();
}

}  // namespace tinyimx::mcp
