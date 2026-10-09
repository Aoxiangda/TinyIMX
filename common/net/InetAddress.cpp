#include "common/net/InetAddress.h"

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>

namespace tinyimx {

InetAddress::InetAddress() {
    std::memset(&address_, 0, sizeof(address_));
    address_.sin_family = AF_INET;
}

InetAddress::InetAddress(const std::string& ip, uint16_t port) {
    SetAddress(ip, port);
}

InetAddress::InetAddress(const sockaddr_in& address)
    : address_(address),
      valid_(true) {}

bool InetAddress::SetAddress(const std::string& ip, uint16_t port) {
    std::memset(&address_, 0, sizeof(address_));

    address_.sin_family = AF_INET;
    address_.sin_port = htons(port);

    if (ip.empty() || ip == "0.0.0.0") {
        address_.sin_addr.s_addr = htonl(INADDR_ANY);
        valid_ = true;
        return true;
    }

    const int ret =
        ::inet_pton(
            AF_INET,
            ip.c_str(),
            &address_.sin_addr
        );

    if (ret == 1) {
        valid_ = true;
        return true;
    }

    /*
     * Production service discovery may advertise stable
     * logical hostnames such as:
     *
     *   gateway-a
     *   gateway-b
     *
     * Keep the IPv4-literal fast path above. Only when
     * inet_pton cannot parse the input do we resolve an
     * IPv4 hostname.
     */
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;

    const int resolve_result =
        ::getaddrinfo(
            ip.c_str(),
            nullptr,
            &hints,
            &results
        );

    if (
        resolve_result != 0 ||
        results == nullptr
    ) {
        valid_ = false;

        if (results != nullptr) {
            ::freeaddrinfo(results);
        }

        return false;
    }

    valid_ = false;

    for (
        addrinfo* current = results;
        current != nullptr;
        current = current->ai_next
    ) {
        if (
            current->ai_family != AF_INET ||
            current->ai_addr == nullptr ||
            current->ai_addrlen <
                static_cast<socklen_t>(
                    sizeof(sockaddr_in)
                )
        ) {
            continue;
        }

        const auto* resolved =
            reinterpret_cast<const sockaddr_in*>(
                current->ai_addr
            );

        address_.sin_addr =
            resolved->sin_addr;

        valid_ = true;
        break;
    }

    ::freeaddrinfo(results);

    return valid_;
}

std::string InetAddress::Ip() const {
    char buffer[INET_ADDRSTRLEN] = {0};

    const char* result = ::inet_ntop(
        AF_INET,
        &address_.sin_addr,
        buffer,
        static_cast<socklen_t>(sizeof(buffer))
    );

    if (result == nullptr) {
        return "";
    }

    return std::string(buffer);
}

uint16_t InetAddress::Port() const {
    return ntohs(address_.sin_port);
}

std::string InetAddress::ToString() const {
    return Ip() + ":" + std::to_string(Port());
}

bool InetAddress::IsValid() const {
    return valid_;
}

const sockaddr* InetAddress::SockAddr() const {
    return reinterpret_cast<const sockaddr*>(&address_);
}

sockaddr* InetAddress::SockAddr() {
    return reinterpret_cast<sockaddr*>(&address_);
}

socklen_t InetAddress::Length() const {
    return static_cast<socklen_t>(sizeof(address_));
}

const sockaddr_in& InetAddress::NativeAddress() const {
    return address_;
}

}  // namespace tinyimx