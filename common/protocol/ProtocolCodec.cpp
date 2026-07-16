#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <cstring>

namespace tinyimx {
namespace {

void AppendUint16(Buffer* buffer, std::uint16_t value) {
    const std::uint16_t network_value = htons(value);
    buffer->Append(&network_value, sizeof(network_value));
}

void AppendUint32(Buffer* buffer, std::uint32_t value) {
    const std::uint32_t network_value = htonl(value);
    buffer->Append(&network_value, sizeof(network_value));
}

std::uint16_t ReadUint16(const char* data) {
    std::uint16_t network_value = 0;
    std::memcpy(&network_value, data, sizeof(network_value));
    return ntohs(network_value);
}

std::uint32_t ReadUint32(const char* data) {
    std::uint32_t network_value = 0;
    std::memcpy(&network_value, data, sizeof(network_value));
    return ntohl(network_value);
}

PacketHeader ParseHeader(const char* data) {
    PacketHeader header;

    header.magic = ReadUint32(data);
    header.version = ReadUint16(data + 4);
    header.type = ReadUint16(data + 6);
    header.flags = ReadUint16(data + 8);
    header.reserved = ReadUint16(data + 10);
    header.seq = ReadUint32(data + 12);
    header.body_size = ReadUint32(data + 16);

    return header;
}

}  // namespace

ProtocolCodec::ProtocolCodec(std::size_t max_body_size)
    : max_body_size_(max_body_size) {}

bool ProtocolCodec::Encode(const Packet& packet,
                           Buffer* output,
                           std::string* error_message) const {
    if (output == nullptr) {
        if (error_message != nullptr) {
            *error_message = "output buffer is null";
        }
        return false;
    }

    if (!ValidatePacketForEncode(packet, error_message)) {
        return false;
    }

    AppendUint32(output, kProtocolMagic);
    AppendUint16(output, kProtocolVersion);
    AppendUint16(output, static_cast<std::uint16_t>(packet.type));
    AppendUint16(output, packet.flags);
    AppendUint16(output, 0);
    AppendUint32(output, packet.seq);
    AppendUint32(output, static_cast<std::uint32_t>(packet.body.size()));

    if (!packet.body.empty()) {
        output->Append(packet.body);
    }

    return true;
}

DecodeResult ProtocolCodec::Decode(Buffer* input) const {
    DecodeResult result;

    if (input == nullptr) {
        result.status = DecodeStatus::kNeedMoreData;
        result.error_message = "input buffer is null";
        return result;
    }

    while (input->ReadableBytes() >= kPacketHeaderSize) {
        const char* header_data = input->Peek();
        const PacketHeader header = ParseHeader(header_data);

        if (header.magic != kProtocolMagic) {
            result.status = DecodeStatus::kInvalidMagic;
            result.error_message = "invalid protocol magic";
            return result;
        }

        if (header.version != kProtocolVersion) {
            result.status = DecodeStatus::kUnsupportedVersion;
            result.error_message = "unsupported protocol version";
            return result;
        }

        if (header.body_size > max_body_size_) {
            result.status = DecodeStatus::kBodyTooLarge;
            result.error_message = "packet body is too large";
            return result;
        }

        const auto message_type =
            static_cast<MessageType>(header.type);

        if (!IsKnownMessageType(message_type)) {
            result.status = DecodeStatus::kInvalidMessageType;
            result.error_message = "invalid message type";
            return result;
        }

        const std::size_t full_packet_size =
            kPacketHeaderSize + static_cast<std::size_t>(header.body_size);

        if (input->ReadableBytes() < full_packet_size) {
            result.status = result.packets.empty()
                ? DecodeStatus::kNeedMoreData
                : DecodeStatus::kOk;
            return result;
        }

        input->Retrieve(kPacketHeaderSize);

        Packet packet;
        packet.type = message_type;
        packet.flags = header.flags;
        packet.seq = header.seq;

        if (header.body_size > 0) {
            packet.body =
                input->RetrieveAsString(
                    static_cast<std::size_t>(header.body_size)
                );
        }

        result.packets.push_back(std::move(packet));
    }

    result.status = result.packets.empty()
        ? DecodeStatus::kNeedMoreData
        : DecodeStatus::kOk;

    return result;
}

std::size_t ProtocolCodec::MaxBodySize() const {
    return max_body_size_;
}

void ProtocolCodec::SetMaxBodySize(std::size_t max_body_size) {
    max_body_size_ = max_body_size;
}

bool ProtocolCodec::ValidatePacketForEncode(
    const Packet& packet,
    std::string* error_message
) const {
    if (!IsKnownMessageType(packet.type)) {
        if (error_message != nullptr) {
            *error_message = "invalid message type";
        }
        return false;
    }

    if (packet.body.size() > max_body_size_) {
        if (error_message != nullptr) {
            *error_message = "packet body is too large";
        }
        return false;
    }

    return true;
}

std::string DecodeStatusToString(DecodeStatus status) {
    switch (status) {
        case DecodeStatus::kOk:
            return "ok";
        case DecodeStatus::kNeedMoreData:
            return "need_more_data";
        case DecodeStatus::kInvalidMagic:
            return "invalid_magic";
        case DecodeStatus::kUnsupportedVersion:
            return "unsupported_version";
        case DecodeStatus::kBodyTooLarge:
            return "body_too_large";
        case DecodeStatus::kInvalidMessageType:
            return "invalid_message_type";
        default:
            return "unknown";
    }
}

}  // namespace tinyimx