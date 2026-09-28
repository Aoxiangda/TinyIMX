#include "common/net/Buffer.h"
#include "services/intelligence/mcp/HttpCodec.h"
#include "services/intelligence/mcp/McpAuth.h"
#include "services/intelligence/mcp/McpDispatcher.h"
#include "services/intelligence/mcp/McpRegistry.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
#define CHECK_TRUE(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x " at line " << __LINE__ << '\n'; ++failures; } } while (0)
#define CHECK_EQ(a,b) CHECK_TRUE((a) == (b))
#define REQUIRE_TRUE(x) do { if (!(x)) { std::cerr << "REQUIRE failed: " #x " at line " << __LINE__ << '\n'; ++failures; return; } } while (0)

using namespace tinyimx::mcp;

Json Meta() {
    return Json{{"io.modelcontextprotocol/protocolVersion", kProtocolVersion},
                {"io.modelcontextprotocol/clientInfo", Json{{"name", "m19-test"}, {"version", "1"}}},
                {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
}

void TestRegistryAndDispatcher() {
    auto registry = std::make_shared<Registry>();
    std::string error;
    REQUIRE_TRUE(registry->RegisterTool({"z.tool", "", "z", {{"type","object"}}, {"mcp.read"}}, [](const RequestContext&, const Json&) { return Json{{"ok", true}}; }, &error));
    REQUIRE_TRUE(registry->RegisterTool({"a.tool", "", "a", {{"type","object"}}, {}}, [](const RequestContext&, const Json&) { return Json{{"ok", true}}; }, &error));
    REQUIRE_TRUE(registry->RegisterResource({"tinyimx://z", "z.resource", "z", "application/json", {}}, [](const RequestContext&, const Json&) { return Json{{"ok", true}}; }, &error));
    REQUIRE_TRUE(registry->RegisterResource({"tinyimx://a", "a.resource", "a", "application/json", {}}, [](const RequestContext&, const Json&) { return Json{{"ok", true}}; }, &error));
    REQUIRE_TRUE(registry->RegisterPrompt({"z.prompt", "", "z", Json::array(), {}}, [](const RequestContext&, const Json&) { return Json{{"messages", Json::array()}}; }, &error));
    REQUIRE_TRUE(registry->RegisterPrompt({"a.prompt", "", "a", Json::array(), {}}, [](const RequestContext&, const Json&) { return Json{{"messages", Json::array()}}; }, &error));
    REQUIRE_TRUE(registry->Freeze(&error));
    CHECK_TRUE(!registry->RegisterTool({"later", "", "", Json::object(), {}}, [](const RequestContext&, const Json&) { return Json(); }, &error));

    const Json list = registry->ListTools();
    REQUIRE_TRUE(list.size() == 2U);
    CHECK_EQ(list.at(0).at("name"), "a.tool");
    CHECK_EQ(list.at(1).at("name"), "z.tool");

    const Json resources = registry->ListResources();
    REQUIRE_TRUE(resources.size() == 2U);
    CHECK_EQ(resources.at(0).at("uri"), "tinyimx://a");
    CHECK_EQ(resources.at(1).at("uri"), "tinyimx://z");

    const Json prompts = registry->ListPrompts();
    REQUIRE_TRUE(prompts.size() == 2U);
    CHECK_EQ(prompts.at(0).at("name"), "a.prompt");
    CHECK_EQ(prompts.at(1).at("name"), "z.prompt");

    Dispatcher dispatcher(registry);
    RequestContext ctx;
    ctx.principal.scopes.insert("mcp.read");
    DispatchMetadata md{kProtocolVersion, "server/discover", ""};
    Json discover{{"jsonrpc","2.0"},{"id",1},{"method","server/discover"},{"params",{{"_meta",Meta()}}}};
    auto result = dispatcher.Dispatch(ctx, md, discover);
    CHECK_EQ(result.http_status, 200);
    CHECK_EQ(result.body.at("result").at("resultType"), "complete");
    CHECK_EQ(result.body.at("result").at("supportedVersions").at(0), kProtocolVersion);

    md.method_header = "tools/call";
    md.name_header = "z.tool";
    Json call{{"jsonrpc","2.0"},{"id",2},{"method","tools/call"},{"params",{{"name","z.tool"},{"arguments",Json::object()},{"_meta",Meta()}}}};
    result = dispatcher.Dispatch(ctx, md, call);
    CHECK_EQ(result.http_status, 200);
    CHECK_TRUE(result.body.at("result").at("structuredContent").at("ok").get<bool>());

    md.name_header = "wrong";
    result = dispatcher.Dispatch(ctx, md, call);
    CHECK_EQ(result.http_status, 400);
    CHECK_EQ(result.body.at("error").at("code"), kHeaderMismatch);
}

void TestAuth() {
    StaticTokenVerifier verifier("secret", 42, "user:42", {"mcp.read"});
    CHECK_TRUE(!verifier.Verify("").ok);
    CHECK_TRUE(!verifier.Verify("Bearer nope").ok);
    const auto ok = verifier.Verify("Bearer secret");
    CHECK_TRUE(ok.ok);
    CHECK_EQ(ok.principal.user_id, 42U);
    CHECK_TRUE(ok.principal.HasScope("mcp.read"));
}

void TestHttpCodec() {
    tinyimx::Buffer buffer;
    const std::string body = R"({"jsonrpc":"2.0"})";
    const std::string request =
        "POST /mcp HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\nMCP-Protocol-Version: 2026-07-28\r\n\r\n" + body;
    buffer.Append(request);
    HttpCodec codec(4096);
    const auto decoded = codec.Decode(&buffer);
    CHECK_EQ(decoded.status, HttpDecodeStatus::kComplete);
    CHECK_EQ(decoded.request.method, "POST");
    CHECK_EQ(decoded.request.Header("mcp-protocol-version"), kProtocolVersion);
    CHECK_EQ(decoded.request.body, body);
    CHECK_EQ(buffer.ReadableBytes(), 0U);

    tinyimx::Buffer bad;
    bad.Append("POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    CHECK_EQ(codec.Decode(&bad).status, HttpDecodeStatus::kUnsupportedTransferEncoding);
}

void TestConcurrentDispatch() {
    auto registry = std::make_shared<Registry>();
    std::atomic<int> calls{0};
    std::string error;
    REQUIRE_TRUE(registry->RegisterTool({"tinyimx.test.concurrent", "", "", {{"type","object"}}, {}}, [&calls](const RequestContext&, const Json&) { calls.fetch_add(1); return Json{{"ok",true}}; }, &error));
    REQUIRE_TRUE(registry->Freeze());
    Dispatcher dispatcher(registry);
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&]() {
            RequestContext ctx;
            DispatchMetadata md{kProtocolVersion, "tools/call", "tinyimx.test.concurrent"};
            Json call{{"jsonrpc","2.0"},{"id",1},{"method","tools/call"},{"params",{{"name","tinyimx.test.concurrent"},{"arguments",Json::object()},{"_meta",Meta()}}}};
            for (int n = 0; n < 100; ++n) CHECK_EQ(dispatcher.Dispatch(ctx, md, call).http_status, 200);
        });
    }
    for (auto& thread : threads) thread.join();
    CHECK_EQ(calls.load(), 800);
}
}

int main() {
    TestRegistryAndDispatcher();
    TestAuth();
    TestHttpCodec();
    TestConcurrentDispatch();
    if (failures != 0) {
        std::cerr << "M19_MCP_CORE_TESTS=FAIL failures=" << failures << '\n';
        return 1;
    }
    std::cout << "M19_MCP_CORE_TESTS=PASS\n";
    return 0;
}
