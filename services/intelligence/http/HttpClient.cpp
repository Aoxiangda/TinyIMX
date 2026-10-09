#include "services/intelligence/http/HttpClient.h"

#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpClient.h"

#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <cctype>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <system_error>
#include <utility>

namespace tinyimx::intelligence {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string Trim(std::string value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool ContainsCrlf(const std::string& value) {
    return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

bool ResolveIpv4(const std::string& host, std::string* ip, std::string* error) {
    if (ip == nullptr) return false;
    in_addr numeric{};
    if (::inet_pton(AF_INET, host.c_str(), &numeric) == 1) {
        *ip = host;
        return true;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (error) *error = std::string("DNS resolution failed for ") + host + ": " + ::gai_strerror(rc);
        if (result) ::freeaddrinfo(result);
        return false;
    }
    char text[INET_ADDRSTRLEN]{};
    const auto* native = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    if (::inet_ntop(AF_INET, &native->sin_addr, text, sizeof(text)) == nullptr) {
        if (error) *error = std::string("inet_ntop failed for ") + host;
        ::freeaddrinfo(result);
        return false;
    }
    *ip = text;
    ::freeaddrinfo(result);
    return true;
}

bool HeaderExists(const HttpHeaders& headers, const std::string& name) {
    const std::string wanted = Lower(name);
    for (const auto& [key, value] : headers) {
        (void)value;
        if (Lower(key) == wanted) return true;
    }
    return false;
}

std::string EncodeRequest(const HttpRequest& request, const ParsedHttpUrl& url, std::string* error) {
    if (request.method.empty() || ContainsCrlf(request.method) || ContainsCrlf(url.target)) {
        if (error) *error = "invalid HTTP method or target";
        return {};
    }
    for (const auto& [name, value] : request.headers) {
        if (name.empty() || ContainsCrlf(name) || ContainsCrlf(value)) {
            if (error) *error = "invalid HTTP header";
            return {};
        }
    }
    std::ostringstream out;
    out << request.method << ' ' << url.target << " HTTP/1.1\r\n";
    out << "Host: " << url.host;
    if (url.port != 80) out << ':' << url.port;
    out << "\r\nConnection: close\r\nUser-Agent: TinyIMX-M19-AI-Agent/1\r\n";
    for (const auto& [name, value] : request.headers) {
        const std::string lower = Lower(name);
        if (lower == "host" || lower == "connection" || lower == "content-length") continue;
        out << name << ": " << value << "\r\n";
    }
    out << "Content-Length: " << request.body.size() << "\r\n";
    if (!request.body.empty() && !HeaderExists(request.headers, "content-type")) {
        out << "Content-Type: application/json\r\n";
    }
    out << "\r\n" << request.body;
    return out.str();
}

}  // namespace

std::string HttpResponse::Header(const std::string& name) const {
    const auto it = headers.find(Lower(name));
    return it == headers.end() ? std::string{} : it->second;
}

HttpClientResult HttpClientResult::Success(HttpResponse response) {
    HttpClientResult out;
    out.ok = true;
    out.response = std::move(response);
    return out;
}

HttpClientResult HttpClientResult::Failure(std::string error) {
    HttpClientResult out;
    out.error = std::move(error);
    return out;
}

bool ParseHttpUrl(const std::string& url, ParsedHttpUrl* output, std::string* error) {
    if (output == nullptr) {
        if (error) *error = "null URL output";
        return false;
    }
    constexpr std::string_view prefix = "http://";
    if (url.rfind(prefix.data(), 0) != 0) {
        if (error) *error = "only http:// endpoints are supported by the M19 native client";
        return false;
    }
    const std::size_t authority_begin = prefix.size();
    const std::size_t path_begin = url.find_first_of("/?", authority_begin);
    const std::string authority = path_begin == std::string::npos
        ? url.substr(authority_begin)
        : url.substr(authority_begin, path_begin - authority_begin);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        if (error) *error = "invalid HTTP authority";
        return false;
    }
    ParsedHttpUrl parsed;
    parsed.scheme = "http";
    parsed.port = 80;
    parsed.target = path_begin == std::string::npos ? "/" : url.substr(path_begin);
    if (!parsed.target.empty() && parsed.target.front() == '?') parsed.target.insert(parsed.target.begin(), '/');
    if (parsed.target.find('#') != std::string::npos) {
        if (error) *error = "URL fragments are not valid HTTP request targets";
        return false;
    }
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        parsed.host = authority.substr(0, colon);
        const std::string port_text = authority.substr(colon + 1);
        unsigned int port = 0;
        const auto [ptr, ec] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (ec != std::errc{} || ptr != port_text.data() + port_text.size() || port == 0 || port > 65535) {
            if (error) *error = "invalid HTTP port";
            return false;
        }
        parsed.port = static_cast<std::uint16_t>(port);
    } else {
        parsed.host = authority;
    }
    if (parsed.host.empty() || ContainsCrlf(parsed.host)) {
        if (error) *error = "invalid HTTP host";
        return false;
    }
    *output = std::move(parsed);
    return true;
}

HttpResponseParser::HttpResponseParser(std::size_t max_body_bytes)
    : max_body_bytes_(max_body_bytes) {}

void HttpResponseParser::Feed(std::string_view bytes) {
    if (complete_ || failed_ || bytes.empty()) return;
    if (buffer_.size() + bytes.size() > max_body_bytes_ + kMaxHeaderBytes + 1024U) {
        Fail("HTTP response exceeds configured limit");
        return;
    }
    buffer_.append(bytes.data(), bytes.size());
    Parse();
}

void HttpResponseParser::FinishOnEof() {
    if (complete_ || failed_) return;
    if (!headers_parsed_ && !ParseHeaders()) {
        if (!failed_) Fail("connection closed before complete HTTP headers");
        return;
    }
    if (failed_) return;
    if (chunked_ || has_content_length_) {
        Parse();
        if (!complete_ && !failed_) Fail("connection closed before complete HTTP response body");
        return;
    }
    const std::size_t body_size = buffer_.size() - body_offset_;
    if (body_size > max_body_bytes_) {
        Fail("HTTP response body exceeds configured limit");
        return;
    }
    response_.body.assign(buffer_.data() + body_offset_, body_size);
    complete_ = true;
}

bool HttpResponseParser::Complete() const noexcept { return complete_; }
bool HttpResponseParser::Failed() const noexcept { return failed_; }
const std::string& HttpResponseParser::Error() const noexcept { return error_; }
const HttpResponse& HttpResponseParser::Response() const noexcept { return response_; }

void HttpResponseParser::Fail(std::string error) {
    if (complete_ || failed_) return;
    failed_ = true;
    error_ = std::move(error);
}

bool HttpResponseParser::ParseHeaders() {
    if (headers_parsed_) return true;
    const std::size_t end = buffer_.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (buffer_.size() > kMaxHeaderBytes) Fail("HTTP response headers exceed limit");
        return false;
    }
    if (end > kMaxHeaderBytes) {
        Fail("HTTP response headers exceed limit");
        return false;
    }
    std::istringstream input(buffer_.substr(0, end));
    std::string status_line;
    if (!std::getline(input, status_line)) {
        Fail("missing HTTP status line");
        return false;
    }
    if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();
    std::istringstream status_stream(status_line);
    std::string version;
    if (!(status_stream >> version >> response_.status) ||
        (version != "HTTP/1.1" && version != "HTTP/1.0")) {
        Fail("invalid HTTP status line");
        return false;
    }
    std::getline(status_stream, response_.reason);
    response_.reason = Trim(response_.reason);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            Fail("malformed HTTP response header");
            return false;
        }
        const std::string key = Lower(Trim(line.substr(0, colon)));
        const std::string value = Trim(line.substr(colon + 1));
        if (key.empty()) {
            Fail("empty HTTP response header name");
            return false;
        }
        const auto it = response_.headers.find(key);
        if (it == response_.headers.end()) response_.headers.emplace(key, value);
        else it->second += "," + value;
    }
    const std::string transfer = Lower(response_.Header("transfer-encoding"));
    if (!transfer.empty()) {
        if (transfer != "chunked") {
            Fail("unsupported HTTP Transfer-Encoding: " + transfer);
            return false;
        }
        chunked_ = true;
    }
    const std::string content_length = response_.Header("content-length");
    if (!content_length.empty()) {
        has_content_length_ = true;
        const auto [ptr, ec] = std::from_chars(
            content_length.data(), content_length.data() + content_length.size(), content_length_);
        if (ec != std::errc{} || ptr != content_length.data() + content_length.size()) {
            Fail("invalid HTTP Content-Length");
            return false;
        }
        if (content_length_ > max_body_bytes_) {
            Fail("HTTP response body exceeds configured limit");
            return false;
        }
    }
    if (chunked_ && has_content_length_) {
        Fail("HTTP response contains both Transfer-Encoding and Content-Length");
        return false;
    }
    body_offset_ = end + 4;
    headers_parsed_ = true;
    return true;
}

bool HttpResponseParser::ParseContentLengthBody() {
    if (buffer_.size() < body_offset_ + content_length_) return false;
    response_.body.assign(buffer_.data() + body_offset_, content_length_);
    complete_ = true;
    return true;
}

bool HttpResponseParser::ParseChunkedBody() {
    std::size_t pos = body_offset_;
    std::string decoded;
    while (true) {
        const std::size_t line_end = buffer_.find("\r\n", pos);
        if (line_end == std::string::npos) return false;
        std::string size_text = buffer_.substr(pos, line_end - pos);
        const auto extension = size_text.find(';');
        if (extension != std::string::npos) size_text.resize(extension);
        size_text = Trim(size_text);
        if (size_text.empty()) {
            Fail("empty HTTP chunk size");
            return false;
        }
        std::size_t chunk_size = 0;
        const auto [ptr, ec] = std::from_chars(
            size_text.data(), size_text.data() + size_text.size(), chunk_size, 16);
        if (ec != std::errc{} || ptr != size_text.data() + size_text.size()) {
            Fail("invalid HTTP chunk size");
            return false;
        }
        pos = line_end + 2;
        if (chunk_size == 0) {
            if (buffer_.size() < pos + 2) return false;
            if (buffer_.compare(pos, 2, "\r\n") == 0) {
                response_.body = std::move(decoded);
                complete_ = true;
                return true;
            }
            const std::size_t trailer_end = buffer_.find("\r\n\r\n", pos);
            if (trailer_end == std::string::npos) return false;
            response_.body = std::move(decoded);
            complete_ = true;
            return true;
        }
        if (chunk_size > max_body_bytes_ - decoded.size()) {
            Fail("HTTP response body exceeds configured limit");
            return false;
        }
        if (buffer_.size() < pos + chunk_size + 2) return false;
        decoded.append(buffer_.data() + pos, chunk_size);
        pos += chunk_size;
        if (buffer_.compare(pos, 2, "\r\n") != 0) {
            Fail("malformed HTTP chunk terminator");
            return false;
        }
        pos += 2;
    }
}

void HttpResponseParser::Parse() {
    if (complete_ || failed_) return;
    if (!headers_parsed_ && !ParseHeaders()) return;
    if (failed_) return;
    if (chunked_) {
        ParseChunkedBody();
    } else if (has_content_length_) {
        ParseContentLengthBody();
    }
}

HttpClientResult NativeHttpClient::Execute(const HttpRequest& request) const {
    if (request.url.empty()) return HttpClientResult::Failure("HTTP URL is empty");
    if (request.connect_timeout.count() <= 0 || request.request_timeout.count() <= 0) {
        return HttpClientResult::Failure("HTTP timeouts must be positive");
    }
    if (request.max_response_bytes == 0) return HttpClientResult::Failure("max_response_bytes must be positive");

    ParsedHttpUrl parsed;
    std::string error;
    if (!ParseHttpUrl(request.url, &parsed, &error)) return HttpClientResult::Failure(error);
    std::string ip;
    if (!ResolveIpv4(parsed.host, &ip, &error)) return HttpClientResult::Failure(error);
    InetAddress address(ip, parsed.port);
    if (!address.IsValid()) return HttpClientResult::Failure("resolved HTTP endpoint is invalid");
    const std::string wire = EncodeRequest(request, parsed, &error);
    if (wire.empty() && !error.empty()) return HttpClientResult::Failure(error);

    EventLoop loop;
    if (!loop.IsValid()) return HttpClientResult::Failure("failed to create HTTP EventLoop");
    HttpResponseParser parser(request.max_response_bytes);
    HttpClientResult result = HttpClientResult::Failure("HTTP request did not complete");
    bool done = false;

    TcpClient client(&loop, address, "tinyimx-ai-http", request.connect_timeout);
    client.SetConnectionCallback([&](const TcpConnectionPtr& connection) {
        if (connection && connection->IsConnected()) {
            connection->Send(wire);
            return;
        }
        if (done) return;
        parser.FinishOnEof();
        result = parser.Complete()
            ? HttpClientResult::Success(parser.Response())
            : HttpClientResult::Failure(parser.Failed() ? parser.Error() : "HTTP connection closed before response completed");
        done = true;
        loop.Quit();
    });
    client.SetMessageCallback([&](const TcpConnectionPtr&, Buffer* buffer) {
        if (done || buffer == nullptr) return;
        parser.Feed(buffer->RetrieveAllAsString());
        if (parser.Failed()) {
            result = HttpClientResult::Failure(parser.Error());
            done = true;
            loop.Quit();
        } else if (parser.Complete()) {
            result = HttpClientResult::Success(parser.Response());
            done = true;
            loop.Quit();
        }
    });
    client.SetConnectErrorCallback([&](int error_number) {
        if (done) return;
        result = HttpClientResult::Failure(std::string("HTTP connect failed: ") + std::strerror(error_number));
        done = true;
        loop.Quit();
    });
    loop.RunAfter(request.request_timeout, [&]() {
        if (done) return;
        result = HttpClientResult::Failure("HTTP request timeout");
        done = true;
        client.Stop();
        loop.Quit();
    });

    if (!client.Connect()) return HttpClientResult::Failure("HTTP client failed to submit connect request");
    loop.Loop();
    client.Stop();
    return done ? result : HttpClientResult::Failure("HTTP EventLoop stopped before request completed");
}

}  // namespace tinyimx::intelligence
