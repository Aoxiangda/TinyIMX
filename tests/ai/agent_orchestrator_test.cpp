#include "services/intelligence/ai/AgentOrchestrator.h"

#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures = 0;
#define CHECK_TRUE(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x " at line " << __LINE__ << '\n'; ++failures; } } while (0)
#define CHECK_EQ(a,b) CHECK_TRUE((a) == (b))

using tinyimx::ai::Json;

class FakeProvider final : public tinyimx::ai::IProvider {
public:
    std::deque<tinyimx::ai::CompletionResult> scripted;
    std::vector<tinyimx::ai::CompletionRequest> requests;

    tinyimx::ai::CompletionResult Complete(const tinyimx::ai::CompletionRequest& request) override {
        requests.push_back(request);
        if (scripted.empty()) return {false,{}, {}, {}, "no scripted completion"};
        auto out = scripted.front();
        scripted.pop_front();
        return out;
    }
};

class FakeMcpClient final : public tinyimx::mcp::IToolClient {
public:
    std::vector<std::pair<std::string,Json>> calls;
    bool call_ok{true};

    tinyimx::mcp::DiscoverResponse Discover() override {
        return {true,{tinyimx::mcp::kProtocolVersion},Json{{"tools",Json::object()}},Json{{"name","TinyIMX MCPServer"}},"",{}};
    }
    tinyimx::mcp::ListToolsResponse ListTools() override {
        return {true,{
            {"tinyimx.user.get_self_profile","","profile",Json{{"type","object"}}},
            {"tinyimx.social.list_friends","","friends",Json{{"type","object"}}}
        },{}};
    }
    tinyimx::mcp::CallToolResponse CallTool(const std::string& name, const Json& args) override {
        calls.emplace_back(name,args);
        if (!call_ok) return {false,false,Json::object(),Json::array(),"rpc unavailable"};
        return {true,false,Json{{"profile",Json{{"user_id",42},{"username","alice"}}}},Json::array(),{}};
    }
};

tinyimx::ai::AgentOptions Options() {
    tinyimx::ai::AgentOptions out;
    out.model = "model";
    out.allowed_tools = {"tinyimx.user.get_self_profile"};
    out.max_tool_rounds = 4;
    out.max_tool_calls_per_round = 2;
    out.max_total_tool_calls = 4;
    out.repeated_identical_call_limit = 3;
    return out;
}

void TestToolThenAnswer() {
    auto provider = std::make_shared<FakeProvider>();
    provider->scripted.push_back({true,"",{{"call-1","tinyimx.user.get_self_profile",Json::object()}},"tool_calls",{}});
    provider->scripted.push_back({true,"Alice is signed in.",{},"stop",{}});
    auto mcp = std::make_shared<FakeMcpClient>();
    tinyimx::ai::AgentOrchestrator agent(Options(),provider,mcp);
    const auto result = agent.Run("Who am I?");
    CHECK_TRUE(result.ok);
    CHECK_EQ(result.answer,"Alice is signed in.");
    CHECK_EQ(result.tool_calls,1U);
    CHECK_EQ(result.tool_rounds,1U);
    CHECK_EQ(mcp->calls.size(),1U);
    CHECK_EQ(provider->requests.size(),2U);
    CHECK_EQ(provider->requests.at(1).messages.at(2).role,"assistant");
    CHECK_EQ(provider->requests.at(1).messages.at(3).role,"tool");
    CHECK_EQ(provider->requests.at(1).messages.at(3).tool_call_id,"call-1");
    const auto tool_result = Json::parse(provider->requests.at(1).messages.at(3).content);
    CHECK_TRUE(!tool_result.at("isError").get<bool>());
    CHECK_EQ(tool_result.at("structuredContent").at("profile").at("user_id"),42);
}

void TestUnknownToolBlocked() {
    auto provider=std::make_shared<FakeProvider>();
    provider->scripted.push_back({true,"",{{"x","tinyimx.message.delete_all",Json::object()}},"tool_calls",{}});
    auto mcp=std::make_shared<FakeMcpClient>();
    tinyimx::ai::AgentOrchestrator agent(Options(),provider,mcp);
    const auto result=agent.Run("delete everything");
    CHECK_TRUE(!result.ok);
    CHECK_TRUE(result.error.find("outside the execution allowlist")!=std::string::npos);
    CHECK_TRUE(mcp->calls.empty());
}

void TestRepeatedCallProtection() {
    auto provider=std::make_shared<FakeProvider>();
    for(int i=0;i<3;++i) provider->scripted.push_back({true,"",{{"r"+std::to_string(i),"tinyimx.user.get_self_profile",Json::object()}},"tool_calls",{}});
    auto mcp=std::make_shared<FakeMcpClient>();
    tinyimx::ai::AgentOrchestrator agent(Options(),provider,mcp);
    const auto result=agent.Run("loop");
    CHECK_TRUE(!result.ok);
    CHECK_TRUE(result.error.find("repeated identical")!=std::string::npos);
    CHECK_EQ(mcp->calls.size(),2U);
}

void TestLimitsAndMcpFailure() {
    auto provider=std::make_shared<FakeProvider>();
    provider->scripted.push_back({true,"",{{"a","tinyimx.user.get_self_profile",Json::object()},{"b","tinyimx.user.get_self_profile",Json{{"x",1}}},{"c","tinyimx.user.get_self_profile",Json{{"x",2}}}},"tool_calls",{}});
    auto mcp=std::make_shared<FakeMcpClient>();
    tinyimx::ai::AgentOrchestrator agent(Options(),provider,mcp);
    auto result=agent.Run("too many");
    CHECK_TRUE(!result.ok);
    CHECK_TRUE(result.error.find("too many tools")!=std::string::npos);
    CHECK_TRUE(mcp->calls.empty());

    provider=std::make_shared<FakeProvider>();
    provider->scripted.push_back({true,"",{{"a","tinyimx.user.get_self_profile",Json::object()}},"tool_calls",{}});
    mcp=std::make_shared<FakeMcpClient>();
    mcp->call_ok=false;
    tinyimx::ai::AgentOrchestrator agent2(Options(),provider,mcp);
    result=agent2.Run("mcp down");
    CHECK_TRUE(!result.ok);
    CHECK_TRUE(result.error.find("rpc unavailable")!=std::string::npos);
}

} // namespace

int main(){
    TestToolThenAnswer();
    TestUnknownToolBlocked();
    TestRepeatedCallProtection();
    TestLimitsAndMcpFailure();
    if(failures){std::cerr<<"M19_AGENT_ORCHESTRATOR_TESTS=FAIL failures="<<failures<<'\n';return 1;}
    std::cout<<"M19_AGENT_ORCHESTRATOR_TESTS=PASS\n";
    return 0;
}
