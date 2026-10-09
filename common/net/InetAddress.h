#pragma once

#include <cstdint>
#include <netinet/in.h>
#include <string>

namespace tinyimx {
    class InetAddress {
        public:
            InetAddress();
            InetAddress(const std::string& ip, uint16_t port);
            explicit InetAddress(const sockaddr_in& addr);

            bool SetAddress(const std::string& ip, uint16_t port);

            std::string Ip() const;
            uint16_t Port() const;
            std::string ToString() const;


            bool IsValid() const;

            const sockaddr* SockAddr() const;
            sockaddr* SockAddr();
            socklen_t Length() const;

            const sockaddr_in& NativeAddress() const;
        private:
            sockaddr_in address_;
            bool valid_{false};
    };
} // namespace tinyimx