#include "services/intelligence/ai/OpenAICompatibleProvider.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK_TRUE(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x " at line " << __LINE__ << '\n'; ++failures; } } while (0)
#define CHECK_EQ(a,b) CHECK_TRUE((a) == (b))

using tinyimx::ai::Json;
using tinyimx::intelligence::HttpClientResult;
using tinyimx::intelligence::HttpRequest;
using tinyimx::intelligence::HttpResponse;

class FakeTransport final : public tinyimx::intelligence::IHttpTransport {
public:
    mutable std::vector<HttpRequest> requests;
    int status{200};
    std::string response_body;
    bool transport_error{false};

    HttpClientResult Execute(const HttpRequest& request) const override {
        requests.push_back(request);
        if (transport_error) return HttpClientResult{false, HttpResponse{}, "timeout"};
        return HttpClientResult{true, HttpResponse{status,"OK",{{"content-type","application/json"}},response_body}, {}};
    }
};

tinyimx::ai::CompletionRequest BaseRequest() {
    tinyimx::ai::CompletionRequest request;
    request.model = "qwen-test";
    request.messages.push_back({"user","Who am I?",{}, {}});
    request.tools.push_back({"tinyimx.user.get_self_profile","profile",Json{{"type","object"}}});
    return request;
}

void TestToolCallAndRequestEncoding() {
    auto transport = std::make_shared<FakeTransport>();
    transport->response_body = Json{
        {"choices",Json::array({Json{
            {"finish_reason","tool_calls"},
            {"message",Json{{"role","assistant"},{"content",nullptr},{"tool_calls",Json::array({Json{
                {"id","call_1"},{"type","function"},
                {"function",Json{{"name","tinyimx.user.get_self_profile"},{"arguments","{}"}}}
            }})}}}
        }})}
    }.dump();

    tinyimx::ai::OpenAICompatibleOptions options;
    options.endpoint = "http://127.0.0.1:11434/v1/chat/completions";
    options.api_key = "key";
    tinyimx::ai::OpenAICompatibleProvider provider(options, transport);
    const auto result = provider.Complete(BaseRequest());
    CHECK_TRUE(result.ok);
    CHECK_EQ(result.tool_calls.size(), 1U);
    CHECK_EQ(result.tool_calls.at(0).name, "tinyimx.user.get_self_profile");
    CHECK_TRUE(result.tool_calls.at(0).arguments.empty());

    CHECK_EQ(transport->requests.size(), 1U);
    CHECK_EQ(transport->requests.at(0).headers.at("Authorization"), "Bearer key");
    const auto sent = Json::parse(transport->requests.at(0).body);
    CHECK_EQ(sent.at("model"), "qwen-test");
    CHECK_EQ(sent.at("tools").at(0).at("type"), "function");
    CHECK_EQ(sent.at("tools").at(0).at("function").at("name"), "tinyimx.user.get_self_profile");
}

void TestFinalAnswerAndErrors() {
    auto transport = std::make_shared<FakeTransport>();
    tinyimx::ai::OpenAICompatibleOptions options;
    options.endpoint = "http://127.0.0.1:11434/v1/chat/completions";
    tinyimx::ai::OpenAICompatibleProvider provider(options, transport);

    transport->response_body = Json{{"choices",Json::array({Json{{"finish_reason","stop"},{"message",Json{{"role","assistant"},{"content","You are Alice."}}}}})}}.dump();
    auto result = provider.Complete(BaseRequest());
    CHECK_TRUE(result.ok);
    CHECK_EQ(result.content, "You are Alice.");

    transport->response_body = "{";
    result = provider.Complete(BaseRequest());
    CHECK_TRUE(!result.ok);
    CHECK_TRUE(result.error.find("malformed JSON") != std::string::npos);

    transport->response_body = Json{{"error",Json{{"message","bad"}}}}.dump();
    transport->status = 500;
    result = provider.Complete(BaseRequest());
    CHECK_TRUE(!result.ok);
    CHECK_TRUE(result.error.find("500") != std::string::npos);

    transport->status = 200;
    transport->transport_error = true;
    result = provider.Complete(BaseRequest());
    CHECK_TRUE(!result.ok);
    CHECK_TRUE(result.error.find("timeout") != std::string::npos);
}
}  // namespace

int main() {
    TestToolCallAndRequestEncoding();
    TestFinalAnswerAndErrors();
    if (failures) {
        std::cerr << "M19_AI_PROVIDER_TESTS=FAIL failures=" << failures << '\n';
        return 1;
    }
    std::cout << "M19_AI_PROVIDER_TESTS=PASS\n";
    return 0;
}
