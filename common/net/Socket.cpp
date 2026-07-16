#include "common/net/Socket.h"

#include "common/logging/LogMacros.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace tinyimx {
namespace {

std::string ErrnoString() {
    return std::strerror(errno);
}

}  // namespace

Socket::Socket() = default;

Socket::Socket(int fd)
    : fd_(fd) {}

Socket::~Socket() {
    Close();
}

Socket::Socket(Socket&& other) noexcept
    : fd_(other.fd_) {
    other.fd_ = kInvalidFd;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Close();

    fd_ = other.fd_;
    other.fd_ = kInvalidFd;

    return *this;
}

bool Socket::CreateTcp() {
    Close();

    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        LOG_ERROR("failed to create tcp socket"
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

bool Socket::Bind(const InetAddress& address) {
    if (!IsValid()) {
        LOG_ERROR("bind failed: invalid socket");
        return false;
    }

    if (!address.IsValid()) {
        LOG_ERROR("bind failed: invalid address");
        return false;
    }

    const int ret = ::bind(fd_, address.SockAddr(), address.Length());
    if (ret != 0) {
        LOG_ERROR("bind failed"
                  << ", fd=" << fd_
                  << ", address=" << address.ToString()
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

bool Socket::Listen(int backlog) {
    if (!IsValid()) {
        LOG_ERROR("listen failed: invalid socket");
        return false;
    }

    const int ret = ::listen(fd_, backlog);
    if (ret != 0) {
        LOG_ERROR("listen failed"
                  << ", fd=" << fd_
                  << ", backlog=" << backlog
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

int Socket::Accept(InetAddress* peer_address) {
    if (!IsValid()) {
        LOG_ERROR("accept failed: invalid socket");
        return kInvalidFd;
    }

    sockaddr_in peer_addr {};
    socklen_t peer_len = static_cast<socklen_t>(sizeof(peer_addr));

    int conn_fd = kInvalidFd;

    do {
        conn_fd = ::accept4(
            fd_,
            reinterpret_cast<sockaddr*>(&peer_addr),
            &peer_len,
            SOCK_NONBLOCK | SOCK_CLOEXEC
        );
    } while (conn_fd < 0 && errno == EINTR);

    if (conn_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return kInvalidFd;
        }

        LOG_ERROR("accept failed"
                  << ", fd=" << fd_
                  << ", error=" << ErrnoString());
        return kInvalidFd;
    }

    if (peer_address != nullptr) {
        *peer_address = InetAddress(peer_addr);
    }

    return conn_fd;
}

bool Socket::SetNonBlocking() {
    if (!IsValid()) {
        LOG_ERROR("set non-blocking failed: invalid socket");
        return false;
    }

    return SetNonBlocking(fd_);
}

bool Socket::SetReuseAddr(bool on) {
    if (!IsValid()) {
        LOG_ERROR("set reuse addr failed: invalid socket");
        return false;
    }

    const int value = on ? 1 : 0;
    const int ret = ::setsockopt(
        fd_,
        SOL_SOCKET,
        SO_REUSEADDR,
        &value,
        static_cast<socklen_t>(sizeof(value))
    );

    if (ret != 0) {
        LOG_ERROR("set reuse addr failed"
                  << ", fd=" << fd_
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

bool Socket::SetReusePort(bool on) {
    if (!IsValid()) {
        LOG_ERROR("set reuse port failed: invalid socket");
        return false;
    }

#ifdef SO_REUSEPORT
    const int value = on ? 1 : 0;
    const int ret = ::setsockopt(
        fd_,
        SOL_SOCKET,
        SO_REUSEPORT,
        &value,
        static_cast<socklen_t>(sizeof(value))
    );

    if (ret != 0) {
        LOG_ERROR("set reuse port failed"
                  << ", fd=" << fd_
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
#else
    if (on) {
        LOG_WARN("SO_REUSEPORT is not supported on this platform");
        return false;
    }

    return true;
#endif
}

bool Socket::SetTcpNoDelay(bool on) {
    if (!IsValid()) {
        LOG_ERROR("set tcp no delay failed: invalid socket");
        return false;
    }

    const int value = on ? 1 : 0;
    const int ret = ::setsockopt(
        fd_,
        IPPROTO_TCP,
        TCP_NODELAY,
        &value,
        static_cast<socklen_t>(sizeof(value))
    );

    if (ret != 0) {
        LOG_ERROR("set tcp no delay failed"
                  << ", fd=" << fd_
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

bool Socket::SetKeepAlive(bool on) {
    if (!IsValid()) {
        LOG_ERROR("set keep alive failed: invalid socket");
        return false;
    }

    const int value = on ? 1 : 0;
    const int ret = ::setsockopt(
        fd_,
        SOL_SOCKET,
        SO_KEEPALIVE,
        &value,
        static_cast<socklen_t>(sizeof(value))
    );

    if (ret != 0) {
        LOG_ERROR("set keep alive failed"
                  << ", fd=" << fd_
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

bool Socket::ShutdownWrite() {
    if (!IsValid()) {
        return false;
    }

    const int ret = ::shutdown(fd_, SHUT_WR);
    if (ret != 0) {
        LOG_ERROR("shutdown write failed"
                  << ", fd=" << fd_
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

void Socket::Close() {
    if (fd_ == kInvalidFd) {
        return;
    }

    const int old_fd = fd_;
    fd_ = kInvalidFd;

    if (::close(old_fd) != 0) {
        LOG_ERROR("close socket failed"
                  << ", fd=" << old_fd
                  << ", error=" << ErrnoString());
    }
}

int Socket::Fd() const {
    return fd_;
}

bool Socket::IsValid() const {
    return fd_ >= 0;
}

bool Socket::SetNonBlocking(int fd) {
    if (fd < 0) {
        return false;
    }

    const int old_flags = ::fcntl(fd, F_GETFL, 0);
    if (old_flags < 0) {
        LOG_ERROR("fcntl F_GETFL failed"
                  << ", fd=" << fd
                  << ", error=" << ErrnoString());
        return false;
    }

    const int new_flags = old_flags | O_NONBLOCK;
    if (::fcntl(fd, F_SETFL, new_flags) != 0) {
        LOG_ERROR("fcntl F_SETFL O_NONBLOCK failed"
                  << ", fd=" << fd
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

bool Socket::SetCloseOnExec(int fd) {
    if (fd < 0) {
        return false;
    }

    const int old_flags = ::fcntl(fd, F_GETFD, 0);
    if (old_flags < 0) {
        LOG_ERROR("fcntl F_GETFD failed"
                  << ", fd=" << fd
                  << ", error=" << ErrnoString());
        return false;
    }

    const int new_flags = old_flags | FD_CLOEXEC;
    if (::fcntl(fd, F_SETFD, new_flags) != 0) {
        LOG_ERROR("fcntl F_SETFD FD_CLOEXEC failed"
                  << ", fd=" << fd
                  << ", error=" << ErrnoString());
        return false;
    }

    return true;
}

}  // namespace tinyimx