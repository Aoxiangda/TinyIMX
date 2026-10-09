#pragma once

#include "common/net/InetAddress.h"

#include <cstdint>

namespace tinyimx {

class Socket {
public:
    Socket();
    explicit Socket(int fd);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    bool CreateTcp();

    bool Bind(const InetAddress& address);
    bool Listen(int backlog);
    int Accept(InetAddress* peer_address);

    bool SetNonBlocking();
    bool SetReuseAddr(bool on);
    bool SetReusePort(bool on);
    bool SetTcpNoDelay(bool on);
    bool SetKeepAlive(bool on);

    bool ShutdownWrite();
    void Close();
    int Release() noexcept;

    int Fd() const;
    bool IsValid() const;

    static bool SetNonBlocking(int fd);
    static bool SetCloseOnExec(int fd);

private:
    static constexpr int kInvalidFd = -1;

    int fd_{kInvalidFd};
};

}  // namespace tinyimx