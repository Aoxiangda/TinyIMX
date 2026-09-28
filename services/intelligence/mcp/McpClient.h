#pragma once

#include "services/intelligence/http/HttpClient.h"
#include "services/intelligence/mcp/McpTypes.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tinyimx::mcp {

struct ClientOptions {
    std::string endpoint{"http://127.0.0.1:8080/mcp"};
    std::string bearer_token;
    std::string client_name{"tinyimx-ai-agent"};
    std::string client_version{"m19"};
    std::chrono::milliseconds connect_timeout{2000};
    std::chrono::milliseconds request_timeout{10000};
    std::size_t max_response_bytes{4U * 1024U * 1024U};
};

struct ClientTool {
    std::string name;
    std::string title;
    std::string description;
    Json input_schema{Json::object()};
};

struct DiscoverResponse {
    bool ok{false};
    std::vector<std::string> supported_versions;
    Json capabilities{Json::object()};
    Json server_info{Json::object()};
    std::string instructions;
    std::string error;
};

struct ListToolsResponse {
    bool ok{false};
    std::vector<ClientTool> tools;
    std::string error;
};

struct CallToolResponse {
    bool ok{false};
    bool is_error{false};
    Json structured_content{Json::object()};
    Json content{Json::array()};
    std::string error;
};

class IToolClient {
public:
    virtual ~IToolClient() = default;
    [[nodiscard]] virtual DiscoverResponse Discover() = 0;
    [[nodiscard]] virtual ListToolsResponse ListTools() = 0;
    [[nodiscard]] virtual CallToolResponse CallTool(
        const std::string& name,
        const Json& arguments) = 0;
};

class Client final : public IToolClient {
public:
    Client(ClientOptions options,
           std::shared_ptr<const intelligence::IHttpTransport> transport);

    [[nodiscard]] DiscoverResponse Discover() override;
    [[nodiscard]] ListToolsResponse ListTools() override;
    [[nodiscard]] CallToolResponse CallTool(
        const std::string& name,
        const Json& arguments) override;

private:
    struct RpcResponse {
        bool ok{false};
        Json result{Json::object()};
        std::string error;
    };

    [[nodiscard]] RpcResponse Request(
        const std::string& method,
        Json params,
        const std::string& name_header = {});
    [[nodiscard]] Json Meta() const;
    [[nodiscard]] std::uint64_t NextId() noexcept;

private:
    ClientOptions options_;
    std::shared_ptr<const intelligence::IHttpTransport> transport_;
    std::atomic<std::uint64_t> next_id_{1};
};

}  // namespace tinyimx::mcp
