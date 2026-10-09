#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tinyimx::intelligence {

using HttpHeaders = std::unordered_map<std::string, std::string>;

struct HttpRequest {
    std::string method{"GET"};
    std::string url;
    HttpHeaders headers;
    std::string body;
    std::chrono::milliseconds connect_timeout{2000};
    std::chrono::milliseconds request_timeout{10000};
    std::size_t max_response_bytes{4U * 1024U * 1024U};
};

struct HttpResponse {
    int status{0};
    std::string reason;
    HttpHeaders headers;
    std::string body;
    [[nodiscard]] std::string Header(const std::string& name) const;
};

struct HttpClientResult {
    bool ok{false};
    HttpResponse response;
    std::string error;
    static HttpClientResult Success(HttpResponse response);
    static HttpClientResult Failure(std::string error);
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    [[nodiscard]] virtual HttpClientResult Execute(const HttpRequest& request) const = 0;
};

struct ParsedHttpUrl {
    std::string scheme;
    std::string host;
    std::uint16_t port{0};
    std::string target{"/"};
};

[[nodiscard]] bool ParseHttpUrl(
    const std::string& url,
    ParsedHttpUrl* output,
    std::string* error = nullptr);

class HttpResponseParser final {
public:
    explicit HttpResponseParser(std::size_t max_body_bytes);
    void Feed(std::string_view bytes);
    void FinishOnEof();
    [[nodiscard]] bool Complete() const noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] const std::string& Error() const noexcept;
    [[nodiscard]] const HttpResponse& Response() const noexcept;

private:
    void Parse();
    void Fail(std::string error);
    bool ParseHeaders();
    bool ParseContentLengthBody();
    bool ParseChunkedBody();

private:
    static constexpr std::size_t kMaxHeaderBytes = 64U * 1024U;
    std::size_t max_body_bytes_{0};
    std::string buffer_;
    HttpResponse response_;
    bool headers_parsed_{false};
    bool chunked_{false};
    bool has_content_length_{false};
    std::size_t content_length_{0};
    std::size_t body_offset_{0};
    bool complete_{false};
    bool failed_{false};
    std::string error_;
};

class NativeHttpClient final : public IHttpTransport {
public:
    [[nodiscard]] HttpClientResult Execute(const HttpRequest& request) const override;
};

}  // namespace tinyimx::intelligence
