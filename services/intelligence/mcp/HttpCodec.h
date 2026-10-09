#pragma once

#include "common/net/Buffer.h"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace tinyimx::mcp {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    [[nodiscard]] std::string Header(const std::string& name) const;
};

enum class HttpDecodeStatus {
    kComplete = 0,
    kNeedMoreData,
    kBadRequest,
    kPayloadTooLarge,
    kUnsupportedTransferEncoding
};

struct HttpDecodeResult {
    HttpDecodeStatus status{HttpDecodeStatus::kNeedMoreData};
    HttpRequest request;
    std::string error;
};

class HttpCodec final {
public:
    explicit HttpCodec(std::size_t max_request_bytes);
    [[nodiscard]] HttpDecodeResult Decode(Buffer* buffer) const;

    [[nodiscard]] static std::string EncodeJsonResponse(
        int status,
        const std::string& body,
        const std::unordered_map<std::string, std::string>& extra_headers = {}
    );

private:
    std::size_t max_request_bytes_;
};

}  // namespace tinyimx::mcp
