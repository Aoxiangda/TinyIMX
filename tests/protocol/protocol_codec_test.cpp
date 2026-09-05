#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace tinyimx::test {
namespace {

Packet MakeLoginRequest() {
    Packet packet;
    packet.type = MessageType::kLoginRequest;
    packet.seq = 1;
    packet.body = R"({"user_id":10001,"token":"demo-token"})";
    return packet;
}

Packet MakeChatMessage() {
    Packet packet;
    packet.type = MessageType::kChatMessage;
    packet.seq = 2;
    packet.body = R"({"from":10001,"to":10002,"text":"hello TinyIMX"})";
    return packet;
}

void AppendUint16(Buffer* buffer, std::uint16_t value) {
    const std::uint16_t network_value = htons(value);
    buffer->Append(&network_value, sizeof(network_value));
}

void AppendUint32(Buffer* buffer, std::uint32_t value) {
    const std::uint32_t network_value = htonl(value);
    buffer->Append(&network_value, sizeof(network_value));
}

void AppendRawHeader(Buffer* buffer,
                     std::uint32_t magic,
                     std::uint16_t version,
                     std::uint16_t type,
                     std::uint16_t flags,
                     std::uint16_t reserved,
                     std::uint32_t seq,
                     std::uint32_t body_size) {
    AppendUint32(buffer, magic);
    AppendUint16(buffer, version);
    AppendUint16(buffer, type);
    AppendUint16(buffer, flags);
    AppendUint16(buffer, reserved);
    AppendUint32(buffer, seq);
    AppendUint32(buffer, body_size);
}

}  // namespace

void RegisterProtocolCodecTests(TestRunner& runner) {
    runner.Add("ProtocolCodec.EncodeDecodeSinglePacket", []() {
        ProtocolCodec codec;
        Buffer buffer;

        std::string error;
        const Packet input_packet = MakeLoginRequest();

        TINYIMX_EXPECT_TRUE(codec.Encode(input_packet, &buffer, &error));

        const DecodeResult result = codec.Decode(&buffer);

        TINYIMX_EXPECT_EQ(result.status, DecodeStatus::kOk);
        TINYIMX_EXPECT_EQ(result.packets.size(), static_cast<std::size_t>(1));

        const Packet& output_packet = result.packets[0];

        TINYIMX_EXPECT_EQ(output_packet.type, MessageType::kLoginRequest);
        TINYIMX_EXPECT_EQ(output_packet.seq, static_cast<std::uint32_t>(1));
        TINYIMX_EXPECT_EQ(output_packet.flags, static_cast<std::uint16_t>(0));
        TINYIMX_EXPECT_EQ(output_packet.body, input_packet.body);
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), static_cast<std::size_t>(0));
    });

    runner.Add("ProtocolCodec.DecodeStickyPackets", []() {
        ProtocolCodec codec;
        Buffer buffer;

        std::string error;

        const Packet login_packet = MakeLoginRequest();
        const Packet chat_packet = MakeChatMessage();

        TINYIMX_EXPECT_TRUE(codec.Encode(login_packet, &buffer, &error));
        TINYIMX_EXPECT_TRUE(codec.Encode(chat_packet, &buffer, &error));

        const DecodeResult result = codec.Decode(&buffer);

        TINYIMX_EXPECT_EQ(result.status, DecodeStatus::kOk);
        TINYIMX_EXPECT_EQ(result.packets.size(), static_cast<std::size_t>(2));

        TINYIMX_EXPECT_EQ(result.packets[0].type, MessageType::kLoginRequest);
        TINYIMX_EXPECT_EQ(result.packets[0].seq, static_cast<std::uint32_t>(1));
        TINYIMX_EXPECT_EQ(result.packets[0].body, login_packet.body);

        TINYIMX_EXPECT_EQ(result.packets[1].type, MessageType::kChatMessage);
        TINYIMX_EXPECT_EQ(result.packets[1].seq, static_cast<std::uint32_t>(2));
        TINYIMX_EXPECT_EQ(result.packets[1].body, chat_packet.body);

        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), static_cast<std::size_t>(0));
    });

    runner.Add("ProtocolCodec.DecodeHalfPacket", []() {
        ProtocolCodec codec;
        Buffer full_buffer;

        std::string error;
        const Packet chat_packet = MakeChatMessage();

        TINYIMX_EXPECT_TRUE(codec.Encode(chat_packet, &full_buffer, &error));

        const std::string raw_bytes = full_buffer.RetrieveAllAsString();

        Buffer half_buffer;

        half_buffer.Append(raw_bytes.data(), 8);

        const DecodeResult first_result = codec.Decode(&half_buffer);

        TINYIMX_EXPECT_EQ(first_result.status, DecodeStatus::kNeedMoreData);
        TINYIMX_EXPECT_EQ(first_result.packets.size(), static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(half_buffer.ReadableBytes(), static_cast<std::size_t>(8));

        half_buffer.Append(
            raw_bytes.data() + 8,
            raw_bytes.size() - 8
        );

        const DecodeResult second_result = codec.Decode(&half_buffer);

        TINYIMX_EXPECT_EQ(second_result.status, DecodeStatus::kOk);
        TINYIMX_EXPECT_EQ(second_result.packets.size(), static_cast<std::size_t>(1));
        TINYIMX_EXPECT_EQ(second_result.packets[0].type, MessageType::kChatMessage);
        TINYIMX_EXPECT_EQ(second_result.packets[0].body, chat_packet.body);
        TINYIMX_EXPECT_EQ(half_buffer.ReadableBytes(), static_cast<std::size_t>(0));
    });

    runner.Add("ProtocolCodec.DecodeInvalidMagic", []() {
        ProtocolCodec codec;
        Buffer buffer;

        AppendRawHeader(
            &buffer,
            0x00000000,
            kProtocolVersion,
            static_cast<std::uint16_t>(MessageType::kHeartbeat),
            0,
            0,
            100,
            0
        );

        const DecodeResult result = codec.Decode(&buffer);

        TINYIMX_EXPECT_EQ(result.status, DecodeStatus::kInvalidMagic);
        TINYIMX_EXPECT_EQ(result.packets.size(), static_cast<std::size_t>(0));

        // 非法包不应该被消费，后续 Gateway 可以直接关闭连接。
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), kPacketHeaderSize);
    });

    runner.Add("ProtocolCodec.DecodeUnsupportedVersion", []() {
        ProtocolCodec codec;
        Buffer buffer;

        AppendRawHeader(
            &buffer,
            kProtocolMagic,
            static_cast<std::uint16_t>(kProtocolVersion + 1),
            static_cast<std::uint16_t>(MessageType::kHeartbeat),
            0,
            0,
            101,
            0
        );

        const DecodeResult result = codec.Decode(&buffer);

        TINYIMX_EXPECT_EQ(result.status, DecodeStatus::kUnsupportedVersion);
        TINYIMX_EXPECT_EQ(result.packets.size(), static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), kPacketHeaderSize);
    });

    runner.Add("ProtocolCodec.DecodeBodyTooLarge", []() {
        ProtocolCodec codec(16);
        Buffer buffer;

        AppendRawHeader(
            &buffer,
            kProtocolMagic,
            kProtocolVersion,
            static_cast<std::uint16_t>(MessageType::kHeartbeat),
            0,
            0,
            102,
            17
        );

        const DecodeResult result = codec.Decode(&buffer);

        TINYIMX_EXPECT_EQ(result.status, DecodeStatus::kBodyTooLarge);
        TINYIMX_EXPECT_EQ(result.packets.size(), static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), kPacketHeaderSize);
    });

    runner.Add("ProtocolCodec.DecodeInvalidMessageType", []() {
        ProtocolCodec codec;
        Buffer buffer;

        AppendRawHeader(
            &buffer,
            kProtocolMagic,
            kProtocolVersion,
            12345,
            0,
            0,
            103,
            0
        );

        const DecodeResult result = codec.Decode(&buffer);

        TINYIMX_EXPECT_EQ(result.status, DecodeStatus::kInvalidMessageType);
        TINYIMX_EXPECT_EQ(result.packets.size(), static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), kPacketHeaderSize);
    });


    runner.Add("ProtocolCodec.UserProfileMessageTypesRoundTrip", []() {
        ProtocolCodec codec;
        Buffer buffer;

        Packet request;
        request.type = MessageType::kUserProfileRequest;
        request.seq = 201;
        request.body = "{}";

        Packet response;
        response.type = MessageType::kUserProfileResponse;
        response.seq = 201;
        response.body =
            R"({"success":true,"profile":{"user_id":10001}})";

        std::string error;
        TINYIMX_EXPECT_TRUE(codec.Encode(request, &buffer, &error));
        TINYIMX_EXPECT_TRUE(codec.Encode(response, &buffer, &error));

        const DecodeResult result = codec.Decode(&buffer);
        TINYIMX_EXPECT_EQ(result.status, DecodeStatus::kOk);
        TINYIMX_EXPECT_EQ(result.packets.size(), static_cast<std::size_t>(2));
        TINYIMX_EXPECT_EQ(result.packets[0].type, MessageType::kUserProfileRequest);
        TINYIMX_EXPECT_EQ(result.packets[1].type, MessageType::kUserProfileResponse);
        TINYIMX_EXPECT_EQ(
            MessageTypeToString(MessageType::kUserProfileRequest),
            std::string("user_profile_request")
        );
        TINYIMX_EXPECT_EQ(
            MessageTypeToString(MessageType::kUserProfileResponse),
            std::string("user_profile_response")
        );
        TINYIMX_EXPECT_TRUE(IsKnownMessageType(MessageType::kUserProfileRequest));
        TINYIMX_EXPECT_TRUE(IsKnownMessageType(MessageType::kUserProfileResponse));
    });

    runner.Add("ProtocolCodec.EncodeRejectsUnknownType", []() {
        ProtocolCodec codec;
        Buffer buffer;

        Packet packet;
        packet.type = MessageType::kUnknown;
        packet.seq = 1;
        packet.body = "{}";

        std::string error;

        TINYIMX_EXPECT_TRUE(!codec.Encode(packet, &buffer, &error));
        TINYIMX_EXPECT_TRUE(!error.empty());
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), static_cast<std::size_t>(0));
    });

    runner.Add("ProtocolCodec.EncodeRejectsTooLargeBody", []() {
        ProtocolCodec codec(8);
        Buffer buffer;

        Packet packet;
        packet.type = MessageType::kChatMessage;
        packet.seq = 1;
        packet.body = "123456789";

        std::string error;

        TINYIMX_EXPECT_TRUE(!codec.Encode(packet, &buffer, &error));
        TINYIMX_EXPECT_TRUE(!error.empty());
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), static_cast<std::size_t>(0));
    });
}

}  // namespace tinyimx::test