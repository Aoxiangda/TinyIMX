#include "services/intelligence/http/HttpClient.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
#define CHECK_TRUE(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x " at line " << __LINE__ << '\n'; ++failures; } } while (0)
#define CHECK_EQ(a,b) CHECK_TRUE((a) == (b))

using namespace tinyimx::intelligence;

void TestUrl() {
    ParsedHttpUrl url;
    std::string error;
    CHECK_TRUE(ParseHttpUrl("http://127.0.0.1:18080/mcp?q=1", &url, &error));
    CHECK_EQ(url.host, "127.0.0.1");
    CHECK_EQ(url.port, 18080U);
    CHECK_EQ(url.target, "/mcp?q=1");
    CHECK_TRUE(!ParseHttpUrl("https://example.com/v1", &url, &error));
    CHECK_TRUE(!ParseHttpUrl("http://host:0/", &url, &error));
    CHECK_TRUE(!ParseHttpUrl("http://host/a#fragment", &url, &error));
}

void TestContentLength() {
    HttpResponseParser parser(1024);
    parser.Feed("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 7\r\n\r\n{\"a\":");
    CHECK_TRUE(!parser.Complete());
    parser.Feed("1}");
    CHECK_TRUE(parser.Complete());
    CHECK_TRUE(!parser.Failed());
    CHECK_EQ(parser.Response().status, 200);
    CHECK_EQ(parser.Response().body, "{\"a\":1}");
    CHECK_EQ(parser.Response().Header("CONTENT-TYPE"), "application/json");
}

void TestChunked() {
    HttpResponseParser parser(1024);
    parser.Feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n");
    CHECK_TRUE(!parser.Complete());
    parser.Feed("5\r\npedia\r\n0\r\n\r\n");
    CHECK_TRUE(parser.Complete());
    CHECK_EQ(parser.Response().body, "Wikipedia");
}

void TestLimitsAndMalformed() {
    HttpResponseParser too_big(3);
    too_big.Feed("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ntest");
    CHECK_TRUE(too_big.Failed());

    HttpResponseParser ambiguous(1024);
    ambiguous.Feed("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    CHECK_TRUE(ambiguous.Failed());

    HttpResponseParser eof(1024);
    eof.Feed("HTTP/1.1 200 OK\r\n\r\nbody-without-length");
    eof.FinishOnEof();
    CHECK_TRUE(eof.Complete());
    CHECK_EQ(eof.Response().body, "body-without-length");
}
}  // namespace

int main() {
    TestUrl();
    TestContentLength();
    TestChunked();
    TestLimitsAndMalformed();
    if (failures) {
        std::cerr << "M19_HTTP_CLIENT_TESTS=FAIL failures=" << failures << '\n';
        return 1;
    }
    std::cout << "M19_HTTP_CLIENT_TESTS=PASS\n";
    return 0;
}
