#pragma once

#include "services/intelligence/ai/AIProvider.h"
#include "services/intelligence/http/HttpClient.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace tinyimx::ai {

struct OpenAICompatibleOptions {
    // Exact Chat Completions endpoint, e.g. http://127.0.0.1:11434/v1/chat/completions.
    std::string endpoint;
    std::string api_key;
    std::chrono::milliseconds connect_timeout{2000};
    std::chrono::milliseconds request_timeout{20000};
    std::size_t max_response_bytes{4U * 1024U * 1024U};
};

class OpenAICompatibleProvider final : public IProvider {
public:
    OpenAICompatibleProvider(
        OpenAICompatibleOptions options,
        std::shared_ptr<const intelligence::IHttpTransport> transport);

    [[nodiscard]] CompletionResult Complete(const CompletionRequest& request) override;

private:
    [[nodiscard]] Json EncodeRequest(const CompletionRequest& request) const;
    [[nodiscard]] CompletionResult DecodeResponse(const Json& response) const;

private:
    OpenAICompatibleOptions options_;
    std::shared_ptr<const intelligence::IHttpTransport> transport_;
};

}  // namespace tinyimx::ai
