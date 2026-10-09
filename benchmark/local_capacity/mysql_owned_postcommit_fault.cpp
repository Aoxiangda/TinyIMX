// Native regression ELF only; never linked into a service/runtime image.
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

extern "C" bool __real_mysql_commit(MYSQL* mysql);
extern "C" bool __wrap_mysql_commit(MYSQL* mysql) {
    const bool failed = __real_mysql_commit(mysql);
    const char* enabled = std::getenv("TINYIMX_TEST_OWN_POSTCOMMIT_FAILURE");
    static std::atomic<bool> injected{false};
    if (failed || !enabled || std::strcmp(enabled, "1") != 0 ||
        injected.exchange(true)) return failed;

    // mysql_commit already received success. This deliberately exercises the
    // application's ambiguous-result branch; it is not wire response loss.
    const int fd = mysql->net.fd;
    int type = 0;
    socklen_t length = sizeof(type);
    sockaddr_in peer{};
    socklen_t peer_length = sizeof(peer);
    in_addr expected{};
    const char* expected_ip = std::getenv("TINYIMX_TEST_OWN_MYSQL_IPV4");
    const bool owned_stream = fd >= 3 && expected_ip &&
        ::inet_pton(AF_INET, expected_ip, &expected) == 1 &&
        ::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) == 0 &&
        type == SOCK_STREAM &&
        ::getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &peer_length) == 0 &&
        peer.sin_family == AF_INET && peer.sin_port == htons(3306) &&
        peer.sin_addr.s_addr == expected.s_addr;
    if (!owned_stream || ::shutdown(fd, SHUT_RDWR) != 0) {
        std::fprintf(stderr, "[FAIL] Owned post-commit socket guard/shutdown failed\n");
        return true;
    }
    // Do not free MYSQL or close fd here: the normal pool path owns cleanup.
    mysql->net.last_errno = CR_SERVER_LOST;
    std::snprintf(mysql->net.last_error, sizeof(mysql->net.last_error),
                  "Owned test: client failure after successful real commit");
    std::snprintf(mysql->net.sqlstate, sizeof(mysql->net.sqlstate), "HY000");
    std::fprintf(stdout,
        "[OBSERVE] OWN_POSTCOMMIT_FAULT real_commit_success=1 own_socket_shutdown=1 failure_return=1\n");
    std::fflush(stdout);
    return true;
}
