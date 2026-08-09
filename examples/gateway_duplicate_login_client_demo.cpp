#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

using Json = nlohmann::json;

constexpr std::uint64_t kTestUserId = 10001;
constexpr const char* kTestUsername = "user10001";
constexpr const char* kTestPassword = "123456";

constexpr std::chrono::milliseconds
    kRedisWaitTimeout{3000};

constexpr std::chrono::milliseconds
    kRedisPollInterval{50};

class ScopedFd {
public:
    explicit ScopedFd(int fd = -1)
        : fd_(fd) {}

    ~ScopedFd() {
        Reset();
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept
        : fd_(other.fd_) {
        other.fd_ = -1;
    }

    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        Reset();

        fd_ = other.fd_;
        other.fd_ = -1;

        return *this;
    }

    int Get() const {
        return fd_;
    }

    bool IsValid() const {
        return fd_ >= 0;
    }

    void Reset(int new_fd = -1) {
        if (fd_ >= 0) {
            ::close(fd_);
        }

        fd_ = new_fd;
    }

private:
    int fd_{-1};
};

struct PacketReader {
    tinyimx::Buffer input_buffer;
    std::deque<tinyimx::Packet> pending_packets;
};

struct RedisOnlineRecord {
    std::uint64_t user_id{0};
    std::string gateway_id;
    std::string connection_name;
    std::int64_t login_time{0};
};

enum class OnlineLookupStatus {
    kFound,
    kMissing,
    kError
};

struct OnlineLookupResult {
    OnlineLookupStatus status{
        OnlineLookupStatus::kError
    };

    RedisOnlineRecord record;
    std::string error_message;
};

bool SetSocketTimeout(
    int fd,
    int seconds
) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    const int recv_result =
        ::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            static_cast<socklen_t>(
                sizeof(timeout)
            )
        );

    const int send_result =
        ::setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            static_cast<socklen_t>(
                sizeof(timeout)
            )
        );

    return recv_result == 0 &&
           send_result == 0;
}

bool SetTcpNoDelay(int fd) {
    int value = 1;

    return ::setsockopt(
               fd,
               IPPROTO_TCP,
               TCP_NODELAY,
               &value,
               static_cast<socklen_t>(
                   sizeof(value)
               )
           ) == 0;
}

int ConnectToServer(
    const std::string& host,
    std::uint16_t port
) {
    const int fd =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (fd < 0) {
        return -1;
    }

    if (!SetSocketTimeout(fd, 5) ||
        !SetTcpNoDelay(fd)) {
        ::close(fd);
        return -1;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr
        ) != 1) {
        ::close(fd);
        return -1;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(
                &address
            ),
            static_cast<socklen_t>(
                sizeof(address)
            )
        ) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool SendAll(
    int fd,
    const std::string& data
) {
    std::size_t sent_total = 0;

    while (sent_total < data.size()) {
        const ssize_t sent =
            ::send(
                fd,
                data.data() + sent_total,
                data.size() - sent_total,
                MSG_NOSIGNAL
            );

        if (sent > 0) {
            sent_total +=
                static_cast<std::size_t>(
                    sent
                );

            continue;
        }

        if (sent < 0 &&
            errno == EINTR) {
            continue;
        }

        std::cerr
            << "send failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }

    return true;
}

bool SendPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    tinyimx::Buffer output;
    std::string error_message;

    if (!codec.Encode(
            packet,
            &output,
            &error_message
        )) {
        std::cerr
            << "encode failed: "
            << error_message
            << '\n';

        return false;
    }

    return SendAll(
        fd,
        output.RetrieveAllAsString()
    );
}

void PrintPacket(
    const std::string& tag,
    const tinyimx::Packet& packet
) {
    std::cout
        << tag
        << " packet:"
        << " type="
        << tinyimx::MessageTypeToString(
            packet.type
        )
        << " seq="
        << packet.seq
        << " body="
        << packet.body
        << '\n';
}

bool ReadNextPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    PacketReader* reader,
    tinyimx::Packet* packet
) {
    if (reader == nullptr ||
        packet == nullptr) {
        return false;
    }

    if (!reader->pending_packets.empty()) {
        *packet =
            std::move(
                reader->
                    pending_packets.front()
            );

        reader->pending_packets.pop_front();

        return true;
    }

    while (true) {
        char temporary_buffer[4096];

        const ssize_t received =
            ::recv(
                fd,
                temporary_buffer,
                sizeof(temporary_buffer),
                0
            );

        if (received > 0) {
            reader->input_buffer.Append(
                temporary_buffer,
                static_cast<std::size_t>(
                    received
                )
            );

            tinyimx::DecodeResult result =
                codec.Decode(
                    &reader->input_buffer
                );

            if (result.status ==
                tinyimx::DecodeStatus::
                    kNeedMoreData) {
                continue;
            }

            if (result.status !=
                tinyimx::DecodeStatus::kOk) {
                std::cerr
                    << "decode failed: "
                    << tinyimx::
                        DecodeStatusToString(
                            result.status
                        )
                    << ", error="
                    << result.error_message
                    << '\n';

                return false;
            }

            for (auto& decoded_packet :
                 result.packets) {
                reader->
                    pending_packets.
                    push_back(
                        std::move(
                            decoded_packet
                        )
                    );
            }

            if (reader->
                    pending_packets.empty()) {
                continue;
            }

            *packet =
                std::move(
                    reader->
                        pending_packets.front()
                );

            reader->
                pending_packets.pop_front();

            return true;
        }

        if (received == 0) {
            std::cerr
                << "server closed connection "
                << "while waiting for packet\n";

            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN ||
            errno == EWOULDBLOCK) {
            std::cerr
                << "recv timeout while "
                << "waiting for packet\n";

            return false;
        }

        std::cerr
            << "recv failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }
}

bool WaitForPacketType(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    PacketReader* reader,
    tinyimx::MessageType expected_type,
    const std::string& client_tag,
    tinyimx::Packet* packet
) {
    constexpr std::size_t
        kMaxSkippedPackets = 128;

    for (std::size_t i = 0;
         i < kMaxSkippedPackets;
         ++i) {
        tinyimx::Packet received_packet;

        if (!ReadNextPacket(
                fd,
                codec,
                reader,
                &received_packet
            )) {
            return false;
        }

        PrintPacket(
            client_tag,
            received_packet
        );

        if (received_packet.type ==
            expected_type) {
            if (packet != nullptr) {
                *packet =
                    std::move(
                        received_packet
                    );
            }

            return true;
        }

        std::cout
            << client_tag
            << " skipped packet while "
            << "waiting for "
            << tinyimx::
                MessageTypeToString(
                    expected_type
                )
            << '\n';
    }

    std::cerr
        << client_tag
        << " exceeded skipped packet limit\n";

    return false;
}

bool WaitForPeerClose(int fd) {
    while (true) {
        char buffer[1024];

        const ssize_t received =
            ::recv(
                fd,
                buffer,
                sizeof(buffer),
                0
            );

        if (received == 0) {
            return true;
        }

        if (received > 0) {
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN ||
            errno == EWOULDBLOCK) {
            std::cerr
                << "timeout waiting for "
                << "server to close old connection\n";

            return false;
        }

        std::cerr
            << "wait for peer close failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }
}

tinyimx::Packet MakeLoginRequest(
    const std::string& username,
    const std::string& password,
    std::uint32_t seq
) {
    tinyimx::Packet packet;
    packet.type =
        tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;

    packet.body =
        Json{
            {"username", username},
            {"password", password}
        }.dump();

    return packet;
}

tinyimx::Packet MakeHeartbeatRequest(
    std::uint32_t seq
) {
    tinyimx::Packet packet;
    packet.type =
        tinyimx::MessageType::kHeartbeat;
    packet.seq = seq;

    packet.body =
        Json{
            {"ping", true}
        }.dump();

    return packet;
}

bool ValidateLoginSuccess(
    const tinyimx::Packet& packet,
    std::uint64_t expected_user_id,
    const std::string& expected_username
) {
    if (packet.type !=
        tinyimx::MessageType::
            kLoginResponse) {
        std::cerr
            << "expected login_response, got "
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }

    try {
        const Json body =
            Json::parse(packet.body);

        if (!body.value(
                "success",
                false
            )) {
            std::cerr
                << "login failed unexpectedly"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value(
                "user_id",
                0ULL
            ) != expected_user_id) {
            std::cerr
                << "login user_id mismatch"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value(
                "username",
                ""
            ) != expected_username) {
            std::cerr
                << "login username mismatch"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse login response failed: "
            << e.what()
            << '\n';

        return false;
    }
}

bool ValidateLoginReplacedError(
    const tinyimx::Packet& packet,
    std::uint64_t expected_user_id
) {
    if (packet.type !=
        tinyimx::MessageType::kError) {
        std::cerr
            << "expected error packet, got "
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }

    try {
        const Json body =
            Json::parse(packet.body);

        if (body.value(
                "success",
                true
            )) {
            std::cerr
                << "expected success=false"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value(
                "reason",
                ""
            ) != "login_replaced") {
            std::cerr
                << "expected "
                << "reason=login_replaced"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value(
                "user_id",
                0ULL
            ) != expected_user_id) {
            std::cerr
                << "replaced user_id mismatch"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse replaced error failed: "
            << e.what()
            << '\n';

        return false;
    }
}

bool ValidateHeartbeatResponse(
    const tinyimx::Packet& packet,
    std::uint32_t expected_seq
) {
    if (packet.type !=
        tinyimx::MessageType::kHeartbeat) {
        std::cerr
            << "expected heartbeat response"
            << ", got="
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }

    if (packet.seq != expected_seq) {
        std::cerr
            << "heartbeat seq mismatch"
            << ", expected="
            << expected_seq
            << ", actual="
            << packet.seq
            << '\n';

        return false;
    }

    try {
        const Json body =
            Json::parse(packet.body);

        if (!body.value(
                "pong",
                false
            )) {
            std::cerr
                << "heartbeat pong missing"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse heartbeat response failed: "
            << e.what()
            << '\n';

        return false;
    }
}

std::string BuildOnlineStatusKey(
    std::uint64_t user_id
) {
    return
        "tinyimx:online:" +
        std::to_string(user_id);
}

OnlineLookupResult ReadOnlineStatus(
    tinyimx::RedisConnectionPool* pool,
    std::uint64_t user_id
) {
    OnlineLookupResult result;

    if (pool == nullptr) {
        result.error_message =
            "redis pool is null";

        return result;
    }

    auto connection =
        pool->Acquire();

    if (!connection) {
        result.error_message =
            "acquire redis connection failed";

        return result;
    }

    const auto value =
        connection->Get(
            BuildOnlineStatusKey(
                user_id
            )
        );

    if (!value.has_value()) {
        if (connection->
                LastError().empty()) {
            result.status =
                OnlineLookupStatus::kMissing;

            return result;
        }

        result.status =
            OnlineLookupStatus::kError;

        result.error_message =
            connection->LastError();

        return result;
    }

    try {
        const Json body =
            Json::parse(
                value.value()
            );

        result.record.user_id =
            body.at("user_id").
                get<std::uint64_t>();

        result.record.gateway_id =
            body.at("gateway_id").
                get<std::string>();

        result.record.connection_name =
            body.at("connection_name").
                get<std::string>();

        result.record.login_time =
            body.at("login_time").
                get<std::int64_t>();

        result.status =
            OnlineLookupStatus::kFound;

        return result;
    } catch (const std::exception& e) {
        result.status =
            OnlineLookupStatus::kError;

        result.error_message =
            std::string(
                "parse redis online status failed: "
            ) +
            e.what();

        return result;
    }
}

void PrintOnlineRecord(
    const std::string& tag,
    const RedisOnlineRecord& record
) {
    std::cout
        << tag
        << " online_record:"
        << " user_id="
        << record.user_id
        << " gateway_id="
        << record.gateway_id
        << " connection_name="
        << record.connection_name
        << " login_time="
        << record.login_time
        << '\n';
}

bool CleanupOnlineStatus(
    tinyimx::RedisConnectionPool* pool,
    std::uint64_t user_id
) {
    if (pool == nullptr) {
        return false;
    }

    auto connection =
        pool->Acquire();

    if (!connection) {
        std::cerr
            << "cleanup online status failed: "
            << "acquire redis connection failed\n";

        return false;
    }

    if (!connection->Del(
            BuildOnlineStatusKey(
                user_id
            )
        )) {
        std::cerr
            << "cleanup online status failed: "
            << connection->LastError()
            << '\n';

        return false;
    }

    return true;
}

using OnlineRecordPredicate =
    std::function<
        bool(const RedisOnlineRecord&)
    >;

bool WaitForOnlineRecord(
    tinyimx::RedisConnectionPool* pool,
    std::uint64_t user_id,
    const OnlineRecordPredicate& predicate,
    std::chrono::milliseconds timeout,
    RedisOnlineRecord* output_record
) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while (true) {
        const OnlineLookupResult result =
            ReadOnlineStatus(
                pool,
                user_id
            );

        if (result.status ==
            OnlineLookupStatus::kError) {
            std::cerr
                << "read online status failed: "
                << result.error_message
                << '\n';

            return false;
        }

        if (result.status ==
                OnlineLookupStatus::kFound &&
            predicate(result.record)) {
            if (output_record != nullptr) {
                *output_record =
                    result.record;
            }

            return true;
        }

        if (std::chrono::
                steady_clock::now() >=
            deadline) {
            break;
        }

        std::this_thread::sleep_for(
            kRedisPollInterval
        );
    }

    std::cerr
        << "timeout waiting for expected "
        << "redis online record"
        << ", user_id="
        << user_id
        << '\n';

    return false;
}

bool VerifyOnlineRecordStable(
    tinyimx::RedisConnectionPool* pool,
    std::uint64_t user_id,
    const RedisOnlineRecord& expected_record,
    std::chrono::milliseconds duration
) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        duration;

    while (true) {
        const OnlineLookupResult result =
            ReadOnlineStatus(
                pool,
                user_id
            );

        if (result.status !=
            OnlineLookupStatus::kFound) {
            std::cerr
                << "online record disappeared "
                << "while verifying old "
                << "connection cleanup";

            if (!result.error_message.empty()) {
                std::cerr
                    << ", error="
                    << result.error_message;
            }

            std::cerr << '\n';

            return false;
        }

        if (result.record.user_id !=
                expected_record.user_id ||
            result.record.gateway_id !=
                expected_record.gateway_id ||
            result.record.connection_name !=
                expected_record.connection_name) {
            std::cerr
                << "online record changed "
                << "unexpectedly\n";

            PrintOnlineRecord(
                "[expected]",
                expected_record
            );

            PrintOnlineRecord(
                "[actual]",
                result.record
            );

            return false;
        }

        if (std::chrono::
                steady_clock::now() >=
            deadline) {
            return true;
        }

        std::this_thread::sleep_for(
            kRedisPollInterval
        );
    }
}

bool WaitForOnlineStatusMissing(
    tinyimx::RedisConnectionPool* pool,
    std::uint64_t user_id,
    std::chrono::milliseconds timeout
) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while (true) {
        const OnlineLookupResult result =
            ReadOnlineStatus(
                pool,
                user_id
            );

        if (result.status ==
            OnlineLookupStatus::kMissing) {
            return true;
        }

        if (result.status ==
            OnlineLookupStatus::kError) {
            std::cerr
                << "read final online status failed: "
                << result.error_message
                << '\n';

            return false;
        }

        if (std::chrono::
                steady_clock::now() >=
            deadline) {
            break;
        }

        std::this_thread::sleep_for(
            kRedisPollInterval
        );
    }

    std::cerr
        << "online status was not removed"
        << ", user_id="
        << user_id
        << '\n';

    return false;
}

bool RunDuplicateLoginRegression(
    const std::string& host,
    std::uint16_t port,
    tinyimx::RedisConnectionPool* redis_pool
) {
    if (!CleanupOnlineStatus(
            redis_pool,
            kTestUserId
        )) {
        return false;
    }

    ScopedFd first_client(
        ConnectToServer(
            host,
            port
        )
    );

    if (!first_client.IsValid()) {
        std::cerr
            << "connect first client failed\n";

        return false;
    }

    ScopedFd second_client(
        ConnectToServer(
            host,
            port
        )
    );

    if (!second_client.IsValid()) {
        std::cerr
            << "connect second client failed\n";

        return false;
    }

    tinyimx::ProtocolCodec codec;

    PacketReader first_reader;
    PacketReader second_reader;

    if (!SendPacket(
            first_client.Get(),
            codec,
            MakeLoginRequest(
                kTestUsername,
                kTestPassword,
                1
            )
        )) {
        return false;
    }

    tinyimx::Packet
        first_login_response;

    if (!WaitForPacketType(
            first_client.Get(),
            codec,
            &first_reader,
            tinyimx::MessageType::
                kLoginResponse,
            "[first_client]",
            &first_login_response
        )) {
        return false;
    }

    if (!ValidateLoginSuccess(
            first_login_response,
            kTestUserId,
            kTestUsername
        )) {
        return false;
    }

    RedisOnlineRecord first_record;

    if (!WaitForOnlineRecord(
            redis_pool,
            kTestUserId,
            [](const RedisOnlineRecord& record) {
                return
                    record.user_id ==
                        kTestUserId &&
                    !record.gateway_id.empty() &&
                    !record.
                        connection_name.empty();
            },
            kRedisWaitTimeout,
            &first_record
        )) {
        return false;
    }

    PrintOnlineRecord(
        "[first_client]",
        first_record
    );

    if (!SendPacket(
            second_client.Get(),
            codec,
            MakeLoginRequest(
                kTestUsername,
                kTestPassword,
                2
            )
        )) {
        return false;
    }

    tinyimx::Packet
        second_login_response;

    if (!WaitForPacketType(
            second_client.Get(),
            codec,
            &second_reader,
            tinyimx::MessageType::
                kLoginResponse,
            "[second_client]",
            &second_login_response
        )) {
        return false;
    }

    if (!ValidateLoginSuccess(
            second_login_response,
            kTestUserId,
            kTestUsername
        )) {
        return false;
    }

    RedisOnlineRecord second_record;

    if (!WaitForOnlineRecord(
            redis_pool,
            kTestUserId,
            [&first_record](
                const RedisOnlineRecord& record
            ) {
                return
                    record.user_id ==
                        kTestUserId &&
                    record.gateway_id ==
                        first_record.gateway_id &&
                    !record.
                        connection_name.empty() &&
                    record.connection_name !=
                        first_record.
                            connection_name;
            },
            kRedisWaitTimeout,
            &second_record
        )) {
        return false;
    }

    PrintOnlineRecord(
        "[second_client]",
        second_record
    );

    tinyimx::Packet replaced_packet;

    if (!WaitForPacketType(
            first_client.Get(),
            codec,
            &first_reader,
            tinyimx::MessageType::kError,
            "[first_client]",
            &replaced_packet
        )) {
        return false;
    }

    if (!ValidateLoginReplacedError(
            replaced_packet,
            kTestUserId
        )) {
        return false;
    }

    if (!WaitForPeerClose(
            first_client.Get()
        )) {
        return false;
    }

    std::cout
        << "first_client_server_close = true\n";

    /*
     * 服务器已经shutdown写端。
     * 客户端现在关闭自己的fd，向服务器发送最终FIN，
     * 促使Gateway进入真实断开回调。
     */
    first_client.Reset();

    if (!VerifyOnlineRecordStable(
            redis_pool,
            kTestUserId,
            second_record,
            std::chrono::milliseconds(
                1200
            )
        )) {
        return false;
    }

    std::cout
        << "redis_after_old_disconnect = "
        << "second_connection_preserved\n";

    constexpr std::uint32_t
        kHeartbeatSeq = 3;

    if (!SendPacket(
            second_client.Get(),
            codec,
            MakeHeartbeatRequest(
                kHeartbeatSeq
            )
        )) {
        return false;
    }

    tinyimx::Packet
        heartbeat_response;

    if (!WaitForPacketType(
            second_client.Get(),
            codec,
            &second_reader,
            tinyimx::MessageType::
                kHeartbeat,
            "[second_client]",
            &heartbeat_response
        )) {
        return false;
    }

    if (!ValidateHeartbeatResponse(
            heartbeat_response,
            kHeartbeatSeq
        )) {
        return false;
    }

    std::cout
        << "second_client_heartbeat = pong\n";

    if (!VerifyOnlineRecordStable(
            redis_pool,
            kTestUserId,
            second_record,
            std::chrono::milliseconds(
                300
            )
        )) {
        return false;
    }

    /*
     * 当前有效连接B主动关闭。
     * Gateway应当执行：
     *
     * UnbindIfCurrent(B)
     * →SetOfflineIfMatch(B)
     * →Redis键被删除
     */
    second_client.Reset();

    if (!WaitForOnlineStatusMissing(
            redis_pool,
            kTestUserId,
            kRedisWaitTimeout
        )) {
        return false;
    }

    std::cout
        << "redis_after_current_disconnect = "
        << "not_found\n";

    return true;
}

}  // namespace

int main(
    int argc,
    char* argv[]
) {
    std::string host =
        "127.0.0.1";

    std::string config_path =
        "config/gateway.json";

    if (argc >= 2) {
        host = argv[1];
    }

    tinyimx::Config config;

    if (argc >= 4) {
        config_path = argv[3];
    }

    if (!config.LoadFromFile(
            config_path
        )) {
        std::cerr
            << "load config failed: "
            << config.LastError()
            << '\n';

        return 1;
    }

    std::uint16_t port =
        config.ServerPort();

    if (argc >= 3) {
        try {
            const int parsed_port =
                std::stoi(argv[2]);

            if (parsed_port <= 0 ||
                parsed_port > 65535) {
                std::cerr
                    << "invalid port: "
                    << argv[2]
                    << '\n';

                return 1;
            }

            port =
                static_cast<std::uint16_t>(
                    parsed_port
                );
        } catch (const std::exception& e) {
            std::cerr
                << "parse port failed: "
                << e.what()
                << '\n';

            return 1;
        }
    }

    tinyimx::LoggerConfig logger_config =
        config.Logger();

    logger_config.file =
        "logs/"
        "gateway_duplicate_login_"
        "client_demo.log";

    if (!tinyimx::Logger::
            Instance().
            Init(logger_config)) {
        std::cerr
            << "logger init failed\n";

        return 1;
    }

    if (!config.Redis().enable) {
        std::cerr
            << "redis is disabled "
            << "in config\n";

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::RedisConnectionPool
        redis_pool;

    if (!redis_pool.Initialize(
            config.Redis()
        )) {
        std::cerr
            << "redis pool init failed\n";

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    std::cout
        << "========== "
        << "Gateway Duplicate Login "
        << "Redis Regression Demo "
        << "==========\n";

    std::cout
        << "gateway="
        << host
        << ':'
        << port
        << ", config="
        << config_path
        << '\n';

    const bool ok =
        RunDuplicateLoginRegression(
            host,
            port,
            &redis_pool
        );

    redis_pool.Shutdown();

    if (!ok) {
        std::cerr
            << "gateway duplicate login "
            << "redis regression failed\n";

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    std::cout
        << "gateway duplicate login "
        << "redis regression passed\n";

    std::cout
        << "=========================="
        << "=========================="
        << "============\n";

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 0;
}