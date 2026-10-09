#include "services/intelligence/mcp/HttpCodec.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>
#include <string_view>

namespace tinyimx::mcp {
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

const char* Reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

}  // namespace

std::string HttpRequest::Header(const std::string& name) const {
    const auto it = headers.find(Lower(name));
    return it == headers.end() ? std::string{} : it->second;
}

HttpCodec::HttpCodec(std::size_t max_request_bytes)
    : max_request_bytes_(max_request_bytes) {}

HttpDecodeResult HttpCodec::Decode(Buffer* buffer) const {
    if (buffer == nullptr) return {HttpDecodeStatus::kBadRequest, {}, "null buffer"};
    const std::size_t readable = buffer->ReadableBytes();
    if (readable > max_request_bytes_) return {HttpDecodeStatus::kPayloadTooLarge, {}, "request exceeds configured limit"};
    const char* begin = buffer->Peek();
    const std::string_view data(begin, readable);
    const std::size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string_view::npos) return {HttpDecodeStatus::kNeedMoreData, {}, {}};

    const std::string header_block(data.substr(0, header_end));
    std::istringstream input(header_block);
    std::string request_line;
    if (!std::getline(input, request_line)) return {HttpDecodeStatus::kBadRequest, {}, "missing request line"};
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();
    std::istringstream line_stream(request_line);
    HttpRequest request;
    if (!(line_stream >> request.method >> request.target >> request.version) || request.version != "HTTP/1.1") {
        return {HttpDecodeStatus::kBadRequest, {}, "invalid HTTP/1.1 request line"};
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) return {HttpDecodeStatus::kBadRequest, {}, "malformed header"};
        const std::string key = Lower(Trim(line.substr(0, colon)));
        const std::string value = Trim(line.substr(colon + 1));
        if (key.empty()) return {HttpDecodeStatus::kBadRequest, {}, "empty header name"};
        if (request.headers.contains(key)) return {HttpDecodeStatus::kBadRequest, {}, "duplicate header"};
        request.headers.emplace(key, value);
    }

    const std::string transfer_encoding = Lower(request.Header("transfer-encoding"));
    if (!transfer_encoding.empty() && transfer_encoding != "identity") {
        return {HttpDecodeStatus::kUnsupportedTransferEncoding, {}, "chunked transfer encoding is not accepted by TinyIMX MCP"};
    }

    std::size_t content_length = 0;
    const std::string content_length_text = request.Header("content-length");
    if (!content_length_text.empty()) {
        const char* first = content_length_text.data();
        const char* last = first + content_length_text.size();
        const auto [ptr, ec] = std::from_chars(first, last, content_length);
        if (ec != std::errc{} || ptr != last) return {HttpDecodeStatus::kBadRequest, {}, "invalid Content-Length"};
    }
    const std::size_t total = header_end + 4 + content_length;
    if (total > max_request_bytes_) return {HttpDecodeStatus::kPayloadTooLarge, {}, "request exceeds configured limit"};
    if (readable < total) return {HttpDecodeStatus::kNeedMoreData, {}, {}};

    request.body.assign(begin + header_end + 4, content_length);
    buffer->Retrieve(total);
    return {HttpDecodeStatus::kComplete, std::move(request), {}};
}

std::string HttpCodec::EncodeJsonResponse(int status, const std::string& body,
                                          const std::unordered_map<std::string, std::string>& extra_headers) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << Reason(status) << "\r\n";
    out << "Content-Type: application/json\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "Cache-Control: no-store\r\n";
    out << "X-Content-Type-Options: nosniff\r\n";
    for (const auto& [name, value] : extra_headers) out << name << ": " << value << "\r\n";
    out << "\r\n" << body;
    return out.str();
}

}  // namespace tinyimx::mcp
