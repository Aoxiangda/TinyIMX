#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <iostream>
#include <string>

namespace {

void PrintDecodeResult(const tinyimx::DecodeResult& result) {
    std::cout << "decode_status = "
              << tinyimx::DecodeStatusToString(result.status) << '\n';

    if (!result.error_message.empty()) {
        std::cout << "error_message = "
                  << result.error_message << '\n';
    }

    std::cout << "packet_count = "
              << result.packets.size() << '\n';

    for (const auto& packet : result.packets) {
        std::cout << "  packet:"
                  << " type=" << tinyimx::MessageTypeToString(packet.type)
                  << " seq=" << packet.seq
                  << " flags=" << packet.flags
                  << " body=" << packet.body
                  << '\n';
    }
}

tinyimx::Packet MakeLoginRequest() {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = 1;
    packet.body = R"({"user_id":10001,"token":"demo-token"})";
    return packet;
}

tinyimx::Packet MakeChatMessage() {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kChatMessage;
    packet.seq = 2;
    packet.body = R"({"from":10001,"to":10002,"text":"hello TinyIMX"})";
    return packet;
}

}  // namespace

int main() {
    tinyimx::ProtocolCodec codec;

    std::cout << "========== ProtocolCodec Demo ==========\n";

    {
        std::cout << "\n[case 1] single full packet\n";

        tinyimx::Buffer buffer;
        std::string error;

        const auto packet = MakeLoginRequest();

        if (!codec.Encode(packet, &buffer, &error)) {
            std::cout << "encode failed: " << error << '\n';
            return 1;
        }

        const auto result = codec.Decode(&buffer);
        PrintDecodeResult(result);
    }

    {
        std::cout << "\n[case 2] sticky packets\n";

        tinyimx::Buffer buffer;
        std::string error;

        codec.Encode(MakeLoginRequest(), &buffer, &error);
        codec.Encode(MakeChatMessage(), &buffer, &error);

        const auto result = codec.Decode(&buffer);
        PrintDecodeResult(result);
    }

    {
        std::cout << "\n[case 3] half packet\n";

        tinyimx::Buffer full_buffer;
        std::string error;

        codec.Encode(MakeChatMessage(), &full_buffer, &error);

        const std::string bytes = full_buffer.RetrieveAllAsString();

        tinyimx::Buffer half_buffer;

        half_buffer.Append(bytes.data(), 8);

        auto first_result = codec.Decode(&half_buffer);
        PrintDecodeResult(first_result);

        half_buffer.Append(
            bytes.data() + 8,
            bytes.size() - 8
        );

        auto second_result = codec.Decode(&half_buffer);
        PrintDecodeResult(second_result);
    }

    {
        std::cout << "\n[case 4] invalid magic\n";

        tinyimx::Buffer buffer;

        const std::string invalid_header(
            tinyimx::kPacketHeaderSize,
            '\0'
        );

        buffer.Append(invalid_header);

        const auto result = codec.Decode(&buffer);
        PrintDecodeResult(result);
    }

    std::cout << "\n========================================\n";

    return 0;
}