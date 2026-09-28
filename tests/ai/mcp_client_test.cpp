#include "services/intelligence/mcp/McpClient.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK_TRUE(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x " at line " << __LINE__ << '\n'; ++failures; } } while (0)
#define CHECK_EQ(a,b) CHECK_TRUE((a) == (b))

using tinyimx::intelligence::HttpClientResult;
using tinyimx::intelligence::HttpRequest;
using tinyimx::intelligence::HttpResponse;
using tinyimx::mcp::Json;

class FakeTransport final : public tinyimx::intelligence::IHttpTransport {
public:
    mutable std::vector<HttpRequest> requests;
    bool bad_json{false};
    bool http_unauthorized{false};

    HttpClientResult Execute(const HttpRequest& request) const override {
        requests.push_back(request);
        if (bad_json) return HttpClientResult{true, HttpResponse{200, "OK", {}, "{"}, {}};
        const Json body = Json::parse(request.body);
        const auto id = body.at("id");
        const auto method = body.at("method").get<std::string>();
        if (http_unauthorized) {
            return HttpClientResult{true, HttpResponse{
                401, "Unauthorized", {{"content-type","application/json"}},
                Json{{"jsonrpc","2.0"},{"id",id},{"error",Json{{"code",-32040},{"message","bad token"}}}}.dump()}, {}};
        }
        Json result;
        if (method == "server/discover") {
            result = Json{{"resultType","complete"},{"supportedVersions",Json::array({tinyimx::mcp::kProtocolVersion})},
                          {"capabilities",Json{{"tools",Json::object()}}},{"serverInfo",Json{{"name","TinyIMX MCPServer"}}}};
        } else if (method == "tools/list") {
            result = Json{{"resultType","complete"},{"tools",Json::array({
                Json{{"name","tinyimx.user.get_self_profile"},{"description","profile"},{"inputSchema",Json{{"type","object"}}}}
            })}};
        } else if (method == "tools/call") {
            result = Json{{"resultType","complete"},{"isError",false},
                          {"structuredContent",Json{{"profile",Json{{"user_id",42}}}}},
                          {"content",Json::array({Json{{"type","text"},{"text","ok"}}})}};
        } else {
            return HttpClientResult{true, HttpResponse{200,"OK",{},
                Json{{"jsonrpc","2.0"},{"id",id},{"error",Json{{"code",-32601},{"message","unknown"}}}}.dump()}, {}};
        }
        return HttpClientResult{true, HttpResponse{200,"OK",{{"content-type","application/json"}},
            Json{{"jsonrpc","2.0"},{"id",id},{"result",result}}.dump()}, {}};
    }
};

void TestHappyPath() {
    auto transport = std::make_shared<FakeTransport>();
    tinyimx::mcp::ClientOptions options;
    options.endpoint = "http://127.0.0.1:18080/mcp";
    options.bearer_token = "secret";
    tinyimx::mcp::Client client(options, transport);

    const auto discover = client.Discover();
    CHECK_TRUE(discover.ok);
    CHECK_EQ(discover.supported_versions.at(0), tinyimx::mcp::kProtocolVersion);

    const auto listed = client.ListTools();
    CHECK_TRUE(listed.ok);
    CHECK_EQ(listed.tools.size(), 1U);
    CHECK_EQ(listed.tools.at(0).name, "tinyimx.user.get_self_profile");

    const auto called = client.CallTool("tinyimx.user.get_self_profile", Json::object());
    CHECK_TRUE(called.ok);
    CHECK_TRUE(!called.is_error);
    CHECK_EQ(called.structured_content.at("profile").at("user_id"), 42);

    CHECK_EQ(transport->requests.size(), 3U);
    const auto& call_request = transport->requests.at(2);
    CHECK_EQ(call_request.headers.at("Authorization"), "Bearer secret");
    CHECK_EQ(call_request.headers.at("MCP-Protocol-Version"), tinyimx::mcp::kProtocolVersion);
    CHECK_EQ(call_request.headers.at("Mcp-Method"), "tools/call");
    CHECK_EQ(call_request.headers.at("Mcp-Name"), "tinyimx.user.get_self_profile");
    const auto body = Json::parse(call_request.body);
    CHECK_EQ(body.at("params").at("_meta").at("io.modelcontextprotocol/protocolVersion"), tinyimx::mcp::kProtocolVersion);
}

void TestFailures() {
    auto transport = std::make_shared<FakeTransport>();
    tinyimx::mcp::ClientOptions options;
    options.endpoint = "http://127.0.0.1:18080/mcp";
    options.bearer_token = "secret";
    tinyimx::mcp::Client client(options, transport);

    transport->http_unauthorized = true;
    const auto unauthorized = client.ListTools();
    CHECK_TRUE(!unauthorized.ok);
    CHECK_TRUE(unauthorized.error.find("401") != std::string::npos);

    transport->http_unauthorized = false;
    transport->bad_json = true;
    const auto bad = client.Discover();
    CHECK_TRUE(!bad.ok);
    CHECK_TRUE(bad.error.find("malformed JSON") != std::string::npos);
}
}  // namespace

int main() {
    TestHappyPath();
    TestFailures();
    if (failures) {
        std::cerr << "M19_MCP_CLIENT_TESTS=FAIL failures=" << failures << '\n';
        return 1;
    }
    std::cout << "M19_MCP_CLIENT_TESTS=PASS\n";
    return 0;
}
