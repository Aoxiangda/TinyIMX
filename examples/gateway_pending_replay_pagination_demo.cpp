#include "common/net/Buffer.h"
#include "common/protocol/ClientChatProtocol.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

std::string MakeClientMessageId(
    const std::string& prefix
) {
    const auto now =
        std::chrono::system_clock::now()
            .time_since_epoch();

    const auto nanoseconds =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(now).count();

    return
        prefix +
        std::to_string(
            nanoseconds
        );
}

bool SetSocketTimeout(
    int fd,
    int seconds
) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        static_cast<socklen_t>(
            sizeof(timeout)
        )
    );

    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &timeout,
        static_cast<socklen_t>(
            sizeof(timeout)
        )
    );

    return true;
}

bool SetTcpNoDelay(
    int fd
) {
    int value = 1;

    return
        ::setsockopt(
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

    SetSocketTimeout(fd, 5);
    SetTcpNoDelay(fd);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (
        ::inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr
        ) != 1
    ) {
        ::close(fd);
        return -1;
    }

    if (
        ::connect(
            fd,
            reinterpret_cast<
                sockaddr*
            >(&address),
            static_cast<socklen_t>(
                sizeof(address)
            )
        ) != 0
    ) {
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

    while (
        sent_total <
        data.size()
    ) {
        const ssize_t n =
            ::send(
                fd,
                data.data() +
                    sent_total,
                data.size() -
                    sent_total,
                MSG_NOSIGNAL
            );

        if (n > 0) {
            sent_total +=
                static_cast<std::size_t>(
                    n
                );
            continue;
        }

        if (
            n < 0 &&
            errno == EINTR
        ) {
            continue;
        }

        return false;
    }

    return true;
}

tinyimx::Packet MakeLoginRequest(
    const std::string& username,
    const std::string& password,
    std::uint32_t seq
) {
    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kLoginRequest;

    packet.seq = seq;

    packet.body =
        Json{
            {"username", username},
            {"password", password}
        }.dump();

    return packet;
}

tinyimx::Packet MakeChatMessage(
    std::uint64_t to,
    std::uint32_t seq,
    const std::string& text,
    const std::string&
        client_message_id
) {
    tinyimx::ClientChatRequest request;

    request.client_message_id =
        client_message_id;

    request.to_user_id = to;
    request.text = text;

    std::string body;
    std::string error_message;

    if (
        !tinyimx::
            SerializeClientChatRequest(
                request,
                &body,
                &error_message
            )
    ) {
        std::cerr
            << "serialize client chat request failed"
            << ", error="
            << error_message
            << '\n';

        return {};
    }

    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kChatMessage;

    packet.seq = seq;
    packet.body = std::move(body);

    return packet;
}

tinyimx::Packet
MakeReceiverChatDeliveryAck(
    std::uint64_t message_id,
    std::uint32_t delivery_seq
) {
    tinyimx::ReceiverChatDeliveryAck ack;
    ack.message_id = message_id;

    std::string body;
    std::string error_message;

    if (
        !tinyimx::
            SerializeReceiverChatDeliveryAck(
                ack,
                &body,
                &error_message
            )
    ) {
        std::cerr
            << "serialize receiver delivery ack failed"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << delivery_seq
            << ", error="
            << error_message
            << '\n';

        return {};
    }

    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kChatDeliveryAck;

    packet.seq = delivery_seq;
    packet.body = std::move(body);

    return packet;
}

bool SendPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    tinyimx::Buffer output;
    std::string error_message;

    if (
        !codec.Encode(
            packet,
            &output,
            &error_message
        )
    ) {
        std::cerr
            << "encode failed: "
            << error_message
            << '\n';

        return false;
    }

    return
        SendAll(
            fd,
            output.
                RetrieveAllAsString()
        );
}

/*
 * TCP是byte stream。
 *
 * 每个Connection必须持久保存自己的Input Buffer；
 * 不能像旧Demo那样每次Wait都创建临时Buffer，
 * 否则函数返回时可能丢失半包。
 */
bool WaitForPackets(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    std::size_t expected_count,
    std::vector<tinyimx::Packet>* packets
) {
    if (
        input_buffer == nullptr ||
        packets == nullptr
    ) {
        return false;
    }

    while (
        packets->size() <
        expected_count
    ) {
        /*
         * 先尝试消费Buffer里之前留下的数据。
         */
        const tinyimx::DecodeResult
            buffered_result =
                codec.Decode(
                    input_buffer
                );

        if (
            buffered_result.status ==
            tinyimx::DecodeStatus::kOk
        ) {
            for (
                const auto& packet :
                    buffered_result.packets
            ) {
                packets->push_back(
                    packet
                );
            }

            if (
                packets->size() >=
                expected_count
            ) {
                return true;
            }
        } else if (
            buffered_result.status !=
            tinyimx::DecodeStatus::
                kNeedMoreData
        ) {
            std::cerr
                << "decode buffered data failed"
                << ", status="
                << tinyimx::
                    DecodeStatusToString(
                        buffered_result.status
                    )
                << ", error="
                << buffered_result.
                    error_message
                << '\n';

            return false;
        }

        char temp[4096];

        const ssize_t n =
            ::recv(
                fd,
                temp,
                sizeof(temp),
                0
            );

        if (n > 0) {
            input_buffer->Append(
                temp,
                static_cast<std::size_t>(
                    n
                )
            );

            continue;
        }

        if (n == 0) {
            std::cerr
                << "server closed connection\n";

            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        std::cerr
            << "recv failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }

    return true;
}

void PrintPacket(
    const std::string& tag,
    const tinyimx::Packet& packet
) {
    std::cout
        << tag
        << " packet:"
        << " type="
        << tinyimx::
            MessageTypeToString(
                packet.type
            )
        << " seq="
        << packet.seq
        << " body="
        << packet.body
        << '\n';
}

bool ValidateReceiverDelivery(
    const tinyimx::Packet& packet,
    const std::string& expected_text,
    std::uint64_t expected_message_id,
    std::uint64_t* actual_message_id,
    std::uint32_t* delivery_seq
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kChatDelivery
    ) {
        return false;
    }

    if (packet.seq == 0) {
        std::cerr
            << "receiver delivery seq must not be zero\n";

        return false;
    }

    tinyimx::ServerChatDelivery delivery;
    std::string error_message;

    if (
        !tinyimx::
            DeserializeServerChatDelivery(
                packet.body,
                &delivery,
                &error_message
            )
    ) {
        std::cerr
            << "deserialize receiver delivery failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    if (
        delivery.from_user_id != 10001 ||
        delivery.to_user_id != 10002 ||
        delivery.text !=
            expected_text ||
        delivery.message_id == 0
    ) {
        std::cerr
            << "receiver delivery business fields mismatch"
            << ", body="
            << packet.body
            << '\n';

        return false;
    }

    if (
        expected_message_id != 0 &&
        delivery.message_id !=
            expected_message_id
    ) {
        std::cerr
            << "offline replay message_id mismatch"
            << ", expected="
            << expected_message_id
            << ", actual="
            << delivery.message_id
            << '\n';

        return false;
    }

    if (
        actual_message_id !=
        nullptr
    ) {
        *actual_message_id =
            delivery.message_id;
    }

    if (
        delivery_seq !=
        nullptr
    ) {
        *delivery_seq =
            packet.seq;
    }

    return true;
}

bool ValidateSenderChatAck(
    const tinyimx::Packet& packet,
    const std::string&
        expected_client_message_id,
    bool expected_reused,
    bool expected_delivered,
    bool expected_stored_offline,
    const std::string& expected_reason,
    std::uint64_t expected_message_id,
    std::uint64_t* actual_message_id
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kChatAck
    ) {
        std::cerr
            << "expected chat_ack"
            << ", actual="
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }

    tinyimx::ClientChatAck ack;
    std::string error_message;

    if (
        !tinyimx::
            DeserializeClientChatAck(
                packet.body,
                &ack,
                &error_message
            )
    ) {
        std::cerr
            << "deserialize chat ack failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    if (!ack.success) {
        std::cerr
            << "chat ack success=false"
            << ", reason="
            << ack.reason
            << '\n';

        return false;
    }

    if (
        ack.client_message_id !=
            expected_client_message_id ||
        ack.message_id == 0 ||
        ack.from_user_id != 10001 ||
        ack.to_user_id != 10002 ||
        ack.reused != expected_reused ||
        ack.delivered !=
            expected_delivered ||
        ack.stored_offline !=
            expected_stored_offline ||
        !ack.stored_persistent ||
        ack.reason !=
            expected_reason
    ) {
        std::cerr
            << "chat ack semantic mismatch"
            << ", expected_reused="
            << expected_reused
            << ", expected_delivered="
            << expected_delivered
            << ", expected_stored_offline="
            << expected_stored_offline
            << ", expected_reason="
            << expected_reason
            << ", body="
            << packet.body
            << '\n';

        return false;
    }

    if (
        expected_message_id != 0 &&
        ack.message_id !=
            expected_message_id
    ) {
        std::cerr
            << "sender ack message_id mismatch"
            << ", expected="
            << expected_message_id
            << ", actual="
            << ack.message_id
            << '\n';

        return false;
    }

    if (
        actual_message_id !=
        nullptr
    ) {
        *actual_message_id =
            ack.message_id;
    }

    return true;
}

bool ExpectNoPacketWithin(
    int fd,
    int timeout_ms
) {
    pollfd descriptor {};
    descriptor.fd = fd;
    descriptor.events = POLLIN;

    int result = 0;

    do {
        result =
            ::poll(
                &descriptor,
                1,
                timeout_ms
            );
    } while (
        result < 0 &&
        errno == EINTR
    );

    if (result == 0) {
        return true;
    }

    if (result < 0) {
        std::cerr
            << "poll failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }

    if (
        descriptor.revents &
        (
            POLLERR |
            POLLHUP |
            POLLNVAL
        )
    ) {
        std::cerr
            << "receiver socket became invalid"
            << ", revents="
            << descriptor.revents
            << '\n';

        return false;
    }

    if (
        descriptor.revents &
        POLLIN
    ) {
        std::cerr
            << "receiver unexpectedly received "
               "another packet after sender retry\n";

        return false;
    }

    return true;
}

/*
 * Receiver登录时可能存在历史Pending消息。
 *
 * 因此不能假设：
 *
 *   user_b_packets[1]
 *
 * 一定就是本次新消息。
 *
 * 这里按稳定server message_id找到目标Replay。
 */
bool WaitForTargetOfflineReplay(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    std::uint64_t expected_message_id,
    const std::string& expected_text,
    tinyimx::Packet* login_response,
    tinyimx::Packet* delivery_packet,
    std::uint32_t* delivery_seq
) {
    if (
        input_buffer == nullptr ||
        login_response == nullptr ||
        delivery_packet == nullptr ||
        delivery_seq == nullptr ||
        expected_message_id == 0
    ) {
        return false;
    }

    bool login_found = false;
    bool delivery_found = false;

    std::vector<tinyimx::Packet>
        received_packets;

    std::size_t scanned = 0;

    for (
        std::size_t round = 0;
        round < 32 &&
        (!login_found || !delivery_found);
        ++round
    ) {
        const std::size_t
            required_count =
                received_packets.size() + 1;

        if (
            !WaitForPackets(
                fd,
                codec,
                input_buffer,
                required_count,
                &received_packets
            )
        ) {
            return false;
        }

        while (
            scanned <
            received_packets.size()
        ) {
            const tinyimx::Packet&
                packet =
                    received_packets[
                        scanned++
                    ];

            PrintPacket(
                "[user_b]",
                packet
            );

            if (
                !login_found &&
                packet.type ==
                    tinyimx::
                        MessageType::
                            kLoginResponse
            ) {
                *login_response =
                    packet;

                login_found = true;

                continue;
            }

            if (
                packet.type !=
                    tinyimx::
                        MessageType::
                            kChatDelivery
            ) {
                continue;
            }

            std::uint64_t
                actual_message_id = 0;

            std::uint32_t
                actual_delivery_seq = 0;

            if (
                !ValidateReceiverDelivery(
                    packet,
                    expected_text,
                    0,
                    &actual_message_id,
                    &actual_delivery_seq
                )
            ) {
                /*
                 * 可能是历史Pending消息，
                 * 不把它当成当前目标失败。
                 */
                continue;
            }

            if (
                actual_message_id !=
                    expected_message_id
            ) {
                std::cout
                    << "[INFO] ignored historical "
                       "offline replay"
                    << ", message_id="
                    << actual_message_id
                    << ", expected="
                    << expected_message_id
                    << '\n';

                continue;
            }

            *delivery_packet =
                packet;

            *delivery_seq =
                actual_delivery_seq;

            delivery_found = true;
        }
    }

    return
        login_found &&
        delivery_found;
}


bool ParseReceiverDeliveryPacket(
    const tinyimx::Packet& packet,
    tinyimx::ServerChatDelivery* delivery
) {
    if (delivery == nullptr) {
        return false;
    }

    if (
        packet.type !=
        tinyimx::MessageType::kChatDelivery
    ) {
        return false;
    }

    if (packet.seq == 0) {
        std::cerr
            << "[FAIL] receiver delivery seq must not be zero\n";

        return false;
    }

    std::string error_message;

    if (
        !tinyimx::DeserializeServerChatDelivery(
            packet.body,
            delivery,
            &error_message
        )
    ) {
        std::cerr
            << "[FAIL] deserialize receiver delivery failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    if (
        delivery->message_id == 0 ||
        delivery->to_user_id != 10002
    ) {
        std::cerr
            << "[FAIL] receiver delivery identity mismatch"
            << ", message_id="
            << delivery->message_id
            << ", to="
            << delivery->to_user_id
            << ", body="
            << packet.body
            << '\n';

        return false;
    }

    return true;
}


bool DrainExistingReceiverBacklog(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    std::size_t* ack_count
) {
    if (
        input_buffer == nullptr ||
        ack_count == nullptr
    ) {
        return false;
    }

    *ack_count = 0;

    std::vector<tinyimx::Packet>
        packets;

    if (
        !WaitForPackets(
            fd,
            codec,
            input_buffer,
            1,
            &packets
        )
    ) {
        return false;
    }

    bool login_found = false;
    std::size_t scanned = 0;

    for (std::size_t round = 0; round < 512; ++round) {
        while (scanned < packets.size()) {
            const tinyimx::Packet&
                packet =
                    packets[scanned++];

            if (
                packet.type ==
                tinyimx::MessageType::
                    kLoginResponse
            ) {
                if (!login_found) {
                    PrintPacket(
                        "[pre-drain receiver]",
                        packet
                    );
                }

                login_found = true;
                continue;
            }

            if (
                packet.type !=
                tinyimx::MessageType::
                    kChatDelivery
            ) {
                continue;
            }

            tinyimx::ServerChatDelivery
                delivery;

            if (
                !ParseReceiverDeliveryPacket(
                    packet,
                    &delivery
                )
            ) {
                return false;
            }

            if (
                !SendPacket(
                    fd,
                    codec,
                    MakeReceiverChatDeliveryAck(
                        delivery.message_id,
                        packet.seq
                    )
                )
            ) {
                std::cerr
                    << "[FAIL] pre-drain receiver ACK failed"
                    << ", message_id="
                    << delivery.message_id
                    << ", delivery_seq="
                    << packet.seq
                    << '\n';

                return false;
            }

            ++(*ack_count);
        }

        pollfd descriptor {};
        descriptor.fd = fd;
        descriptor.events = POLLIN;

        int poll_result = 0;

        do {
            poll_result =
                ::poll(
                    &descriptor,
                    1,
                    350
                );
        } while (
            poll_result < 0 &&
            errno == EINTR
        );

        if (poll_result == 0) {
            return login_found;
        }

        if (poll_result < 0) {
            std::cerr
                << "[FAIL] pre-drain poll failed: "
                << std::strerror(errno)
                << '\n';

            return false;
        }

        if (
            descriptor.revents &
            (
                POLLERR |
                POLLHUP |
                POLLNVAL
            )
        ) {
            std::cerr
                << "[FAIL] pre-drain receiver socket invalid"
                << ", revents="
                << descriptor.revents
                << '\n';

            return false;
        }

        if (
            descriptor.revents &
            POLLIN
        ) {
            const std::size_t
                required_count =
                    packets.size() + 1;

            if (
                !WaitForPackets(
                    fd,
                    codec,
                    input_buffer,
                    required_count,
                    &packets
                )
            ) {
                return false;
            }
        }
    }

    std::cerr
        << "[FAIL] pre-drain exceeded safety round limit\n";

    return false;
}


bool ReceiveExpectedReplayAndAck(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    const std::unordered_set<std::uint64_t>&
        expected_message_ids,
    const std::unordered_map<
        std::uint64_t,
        std::string
    >& expected_message_texts,
    std::unordered_set<std::uint64_t>*
        received_message_ids,
    std::size_t* expected_ack_count,
    std::size_t* duplicate_expected_count,
    std::size_t* historical_ack_count
) {
    if (
        input_buffer == nullptr ||
        received_message_ids == nullptr ||
        expected_ack_count == nullptr ||
        duplicate_expected_count == nullptr ||
        historical_ack_count == nullptr
    ) {
        return false;
    }

    received_message_ids->clear();
    *expected_ack_count = 0;
    *duplicate_expected_count = 0;
    *historical_ack_count = 0;

    std::vector<tinyimx::Packet>
        packets;

    std::size_t scanned = 0;
    bool login_found = false;
    bool login_offline_count_valid = false;

    for (
        std::size_t round = 0;
        round < 2048 &&
        (
            !login_found ||
            received_message_ids->size() <
                expected_message_ids.size()
        );
        ++round
    ) {
        const std::size_t
            required_count =
                packets.size() + 1;

        if (
            !WaitForPackets(
                fd,
                codec,
                input_buffer,
                required_count,
                &packets
            )
        ) {
            std::cerr
                << "[FAIL] receiver replay wait failed"
                << ", received_expected="
                << received_message_ids->size()
                << ", expected="
                << expected_message_ids.size()
                << '\n';

            return false;
        }

        while (scanned < packets.size()) {
            const tinyimx::Packet&
                packet =
                    packets[scanned++];

            if (
                packet.type ==
                tinyimx::MessageType::
                    kLoginResponse
            ) {
                if (!login_found) {
                    PrintPacket(
                        "[user_b]",
                        packet
                    );

                    try {
                        const Json body =
                            Json::parse(
                                packet.body
                            );

                        const std::size_t
                            offline_count =
                                body.value(
                                    "offline_count",
                                    static_cast<std::size_t>(0)
                                );

                        if (
                            !body.value(
                                "success",
                                false
                            ) ||
                            offline_count !=
                                expected_message_ids.size()
                        ) {
                            std::cerr
                                << "[FAIL] login offline_count mismatch"
                                << ", expected="
                                << expected_message_ids.size()
                                << ", actual="
                                << offline_count
                                << ", body="
                                << packet.body
                                << '\n';

                            return false;
                        }

                        login_offline_count_valid =
                            true;

                        std::cout
                            << "[PASS] login offline_count exact"
                            << ", offline_count="
                            << offline_count
                            << '\n';
                    } catch (
                        const std::exception& error
                    ) {
                        std::cerr
                            << "[FAIL] parse login response failed"
                            << ", error="
                            << error.what()
                            << ", body="
                            << packet.body
                            << '\n';

                        return false;
                    }
                }

                login_found = true;
                continue;
            }

            if (
                packet.type !=
                tinyimx::MessageType::
                    kChatDelivery
            ) {
                continue;
            }

            tinyimx::ServerChatDelivery
                delivery;

            if (
                !ParseReceiverDeliveryPacket(
                    packet,
                    &delivery
                )
            ) {
                return false;
            }

            const bool
                belongs_to_current_batch =
                    expected_message_ids.find(
                        delivery.message_id
                    ) !=
                    expected_message_ids.end();

            if (belongs_to_current_batch) {
                const auto
                    text_it =
                        expected_message_texts.find(
                            delivery.message_id
                        );

                if (
                    delivery.from_user_id != 10001 ||
                    text_it ==
                        expected_message_texts.end() ||
                    delivery.text !=
                        text_it->second
                ) {
                    std::cerr
                        << "[FAIL] current batch replay payload mismatch"
                        << ", message_id="
                        << delivery.message_id
                        << ", from="
                        << delivery.from_user_id
                        << ", text="
                        << delivery.text
                        << '\n';

                    return false;
                }

                const bool inserted =
                    received_message_ids->
                        insert(
                            delivery.message_id
                        ).
                        second;

                if (inserted) {
                    ++(*expected_ack_count);
                } else {
                    ++(*duplicate_expected_count);
                }
            } else {
                ++(*historical_ack_count);
            }

            if (
                !SendPacket(
                    fd,
                    codec,
                    MakeReceiverChatDeliveryAck(
                        delivery.message_id,
                        packet.seq
                    )
                )
            ) {
                std::cerr
                    << "[FAIL] receiver replay ACK failed"
                    << ", message_id="
                    << delivery.message_id
                    << ", delivery_seq="
                    << packet.seq
                    << '\n';

                return false;
            }
        }
    }

    return
        login_found &&
        login_offline_count_valid &&
        received_message_ids->size() ==
            expected_message_ids.size();
}


}  // namespace

int main(
    int argc,
    char* argv[]
) {
    std::string host =
        "127.0.0.1";

    std::uint16_t port =
        9001;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3) {
        const int parsed_port =
            std::stoi(argv[2]);

        if (
            parsed_port <= 0 ||
            parsed_port > 65535
        ) {
            std::cerr
                << "invalid port\n";

            return 1;
        }

        port =
            static_cast<
                std::uint16_t
            >(parsed_port);
    }

    std::cout
        << "========== TinyIMX M12 "
           "Pending Replay Pagination Demo ==========\n";

    tinyimx::ProtocolCodec codec;

    constexpr std::size_t
        kBatchMessageCount = 105;

    /*
     * ============================================================
     * 0. Pre-test isolation
     * ============================================================
     *
     * Demo允许重复执行。
     *
     * 先让Receiver登录，把历史Pending全部真实ACK掉，
     * 再断开Receiver。
     *
     * 这样随后创建的105条消息就是本轮唯一Pending，
     * Gateway日志应稳定表现为：
     *
     *   Page #1 = 100
     *   Page #2 = 5
     *
     * 不通过DELETE SQL清测试数据。
     */
    {
        const int pre_receiver_fd =
            ConnectToServer(
                host,
                port
            );

        if (pre_receiver_fd < 0) {
            std::cerr
                << "[FAIL] connect pre-drain receiver failed\n";

            return 1;
        }

        tinyimx::Buffer
            pre_receiver_input_buffer;

        if (
            !SendPacket(
                pre_receiver_fd,
                codec,
                MakeLoginRequest(
                    "user10002",
                    "123456",
                    1
                )
            )
        ) {
            ::close(pre_receiver_fd);
            return 1;
        }

        std::size_t
            pre_drain_ack_count = 0;

        if (
            !DrainExistingReceiverBacklog(
                pre_receiver_fd,
                codec,
                &pre_receiver_input_buffer,
                &pre_drain_ack_count
            )
        ) {
            std::cerr
                << "[FAIL] pre-test backlog drain failed\n";

            ::close(pre_receiver_fd);
            return 1;
        }

        std::cout
            << "[PASS] pre-test receiver backlog drained"
            << ", ack_count="
            << pre_drain_ack_count
            << '\n';

        /*
         * ACK没有ACK-of-ACK。
         *
         * 给Gateway处理最后一批Receiver ACK一个短窗口，
         * 然后关闭Receiver，确保正式fixture创建时它已离线。
         */
        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                500
            )
        );

        ::close(pre_receiver_fd);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                500
            )
        );
    }


    /*
     * ============================================================
     * 1. Sender登录，Receiver保持离线
     * ============================================================
     */
    const int user_a_fd =
        ConnectToServer(
            host,
            port
        );

    if (user_a_fd < 0) {
        std::cerr
            << "[FAIL] connect user A failed\n";

        return 1;
    }

    tinyimx::Buffer
        user_a_input_buffer;

    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeLoginRequest(
                "user10001",
                "123456",
                1
            )
        )
    ) {
        ::close(user_a_fd);
        return 1;
    }

    std::vector<tinyimx::Packet>
        user_a_login_packets;

    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            &user_a_input_buffer,
            1,
            &user_a_login_packets
        )
    ) {
        ::close(user_a_fd);
        return 1;
    }

    PrintPacket(
        "[user_a]",
        user_a_login_packets.front()
    );

    if (
        user_a_login_packets.front().
            type !=
        tinyimx::MessageType::
            kLoginResponse
    ) {
        std::cerr
            << "[FAIL] user A login response invalid\n";

        ::close(user_a_fd);
        return 1;
    }


    /*
     * ============================================================
     * 2. 真实创建105条Persistent Pending
     * ============================================================
     */
    const std::string
        batch_id =
            MakeClientMessageId(
                "m12-pagination-"
            );

    std::unordered_set<std::uint64_t>
        expected_message_ids;

    std::unordered_map<
        std::uint64_t,
        std::string
    > expected_message_texts;

    std::uint32_t sender_seq = 2;

    std::uint64_t first_message_id = 0;
    std::uint64_t last_message_id = 0;

    for (
        std::size_t index = 0;
        index < kBatchMessageCount;
        ++index
    ) {
        const std::string
            client_message_id =
                batch_id +
                "-" +
                std::to_string(index);

        const std::string
            message_text =
                "m12 pending pagination message " +
                std::to_string(index);

        if (
            !SendPacket(
                user_a_fd,
                codec,
                MakeChatMessage(
                    10002,
                    sender_seq,
                    message_text,
                    client_message_id
                )
            )
        ) {
            std::cerr
                << "[FAIL] send offline batch message failed"
                << ", index="
                << index
                << '\n';

            ::close(user_a_fd);
            return 1;
        }

        std::vector<tinyimx::Packet>
            ack_packets;

        if (
            !WaitForPackets(
                user_a_fd,
                codec,
                &user_a_input_buffer,
                1,
                &ack_packets
            )
        ) {
            std::cerr
                << "[FAIL] sender batch ACK missing"
                << ", index="
                << index
                << '\n';

            ::close(user_a_fd);
            return 1;
        }

        std::uint64_t
            server_message_id = 0;

        if (
            !ValidateSenderChatAck(
                ack_packets.front(),
                client_message_id,
                false,  // reused
                false,  // delivered
                true,   // stored_offline
                "target_user_offline",
                0,
                &server_message_id
            )
        ) {
            std::cerr
                << "[FAIL] sender batch ACK invalid"
                << ", index="
                << index
                << '\n';

            ::close(user_a_fd);
            return 1;
        }

        if (server_message_id == 0) {
            std::cerr
                << "[FAIL] batch message_id=0"
                << ", index="
                << index
                << '\n';

            ::close(user_a_fd);
            return 1;
        }

        const bool inserted =
            expected_message_ids.
                insert(
                    server_message_id
                ).
                second;

        if (!inserted) {
            std::cerr
                << "[FAIL] duplicate server message_id "
                   "during batch creation"
                << ", message_id="
                << server_message_id
                << ", index="
                << index
                << '\n';

            ::close(user_a_fd);
            return 1;
        }

        expected_message_texts.emplace(
            server_message_id,
            message_text
        );

        if (index == 0) {
            first_message_id =
                server_message_id;
        }

        last_message_id =
            server_message_id;

        ++sender_seq;
    }

    if (
        expected_message_ids.size() !=
            kBatchMessageCount
    ) {
        std::cerr
            << "[FAIL] persisted batch size mismatch"
            << ", expected="
            << kBatchMessageCount
            << ", actual="
            << expected_message_ids.size()
            << '\n';

        ::close(user_a_fd);
        return 1;
    }

    std::cout
        << "[PASS] persisted offline batch"
        << ", count="
        << expected_message_ids.size()
        << ", first_message_id="
        << first_message_id
        << ", last_message_id="
        << last_message_id
        << ", batch_id="
        << batch_id
        << '\n';

    /*
     * 后半场只验证Persistent Replay，
     * Sender无需继续在线。
     */
    ::close(user_a_fd);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            300
        )
    );


    /*
     * ============================================================
     * 3. Receiver只登录一次
     * ============================================================
     *
     * 这里会触发：
     *
     * Page #1:
     *   message_id > 0
     *   LIMIT 100
     *
     * Page #2:
     *   message_id > Page1.last_message_id
     *   LIMIT 100
     *
     * 本轮fixture应得到100 + 5。
     */
    const int user_b_fd =
        ConnectToServer(
            host,
            port
        );

    if (user_b_fd < 0) {
        std::cerr
            << "[FAIL] connect user B failed\n";

        return 1;
    }

    tinyimx::Buffer
        user_b_input_buffer;

    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                1
            )
        )
    ) {
        ::close(user_b_fd);
        return 1;
    }

    std::unordered_set<std::uint64_t>
        received_message_ids;

    std::size_t expected_ack_count = 0;
    std::size_t duplicate_expected_count = 0;
    std::size_t historical_ack_count = 0;

    if (
        !ReceiveExpectedReplayAndAck(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            expected_message_ids,
            expected_message_texts,
            &received_message_ids,
            &expected_ack_count,
            &duplicate_expected_count,
            &historical_ack_count
        )
    ) {
        std::cerr
            << "[FAIL] receiver did not observe full batch"
            << ", expected="
            << expected_message_ids.size()
            << ", actual="
            << received_message_ids.size()
            << '\n';

        ::close(user_b_fd);
        return 1;
    }


    /*
     * ============================================================
     * 4. Network-level pagination acceptance
     * ============================================================
     */
    bool ok = true;

    if (
        received_message_ids ==
        expected_message_ids
    ) {
        std::cout
            << "[PASS] receiver observed all stable message_ids"
            << ", count="
            << received_message_ids.size()
            << '\n';
    } else {
        std::cerr
            << "[FAIL] receiver stable message_id set mismatch\n";

        ok = false;
    }

    if (
        expected_ack_count ==
            kBatchMessageCount
    ) {
        std::cout
            << "[PASS] receiver ACKed every batch message"
            << ", ack_count="
            << expected_ack_count
            << '\n';
    } else {
        std::cerr
            << "[FAIL] batch receiver ACK count mismatch"
            << ", expected="
            << kBatchMessageCount
            << ", actual="
            << expected_ack_count
            << '\n';

        ok = false;
    }

    if (
        duplicate_expected_count == 0
    ) {
        std::cout
            << "[PASS] no duplicate business message "
               "observed during pagination replay\n";
    } else {
        std::cerr
            << "[FAIL] duplicate batch business message observed"
            << ", duplicate_count="
            << duplicate_expected_count
            << '\n';

        ok = false;
    }

    std::cout
        << "[INFO] historical replay packets ACKed during "
           "current batch receive"
        << ", count="
        << historical_ack_count
        << '\n';


    /*
     * ACK没有ACK-of-ACK。
     *
     * 给Gateway完成durable ReceiverConfirmed一个短窗口，
     * 再跨过原1500ms Receiver ACK timeout窗口，
     * 确保当前105条不会继续产生Retry Delivery。
     */
    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            300
        )
    );

    if (
        ExpectNoPacketWithin(
            user_b_fd,
            1800
        )
    ) {
        std::cout
            << "[PASS] receiver ACK stopped replay retry "
               "after pagination batch\n";
    } else {
        ok = false;
    }


    std::cout
        << "verification_batch_id="
        << batch_id
        << '\n';

    std::cout
        << "verification_expected_message_count="
        << expected_message_ids.size()
        << '\n';

    std::cout
        << "verification_received_unique_message_count="
        << received_message_ids.size()
        << '\n';

    std::cout
        << "verification_receiver_ack_count="
        << expected_ack_count
        << '\n';

    std::cout
        << "verification_duplicate_business_message_count="
        << duplicate_expected_count
        << '\n';

    ::close(user_b_fd);

    if (!ok) {
        std::cerr
            << "gateway pending replay pagination "
               "validation failed\n";

        return 1;
    }

    std::cout
        << "[PASS] pending replay crossed repository page boundary\n"
        << "gateway pending replay pagination validation passed\n"
        << "=================================================\n";

    return 0;
}
