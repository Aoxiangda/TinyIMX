#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpServer.h"
#include "common/protocol/ProtocolCodec.h"

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

namespace {

tinyimx::EventLoop* g_loop = nullptr;

void HandleSignal(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        if (g_loop != nullptr) {
            g_loop->Quit();
        }
    }
}

tinyimx::Packet MakeErrorPacket(std::uint32_t seq,
                                const std::string& message) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kError;
    packet.seq = seq;
    packet.body = std::string(R"({"success":false,"message":")") +
                  message + R"("})";
    return packet;
}

tinyimx::Packet HandlePacket(const tinyimx::Packet& request) {
    tinyimx::Packet response;
    response.seq = request.seq;

    switch (request.type) {
        case tinyimx::MessageType::kLoginRequest:
            response.type = tinyimx::MessageType::kLoginResponse;
            response.body = R"({"success":true,"message":"login accepted"})";
            return response;

        case tinyimx::MessageType::kChatMessage:
            response.type = tinyimx::MessageType::kChatAck;
            response.body = request.body;
            return response;

        case tinyimx::MessageType::kHeartbeat:
            response.type = tinyimx::MessageType::kHeartbeat;
            response.body = R"({"pong":true})";
            return response;

        default:
            return MakeErrorPacket(
                request.seq,
                "unsupported message type"
            );
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "load config failed: "
                  << config.LastError() << '\n';
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 1;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    {
        tinyimx::InetAddress listen_address(
            config.ServerHost(),
            config.ServerPort()
        );

        if (!listen_address.IsValid()) {
            LOG_ERROR("invalid listen address"
                      << ", host=" << config.ServerHost()
                      << ", port=" << config.ServerPort());

            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::EventLoop loop;
        g_loop = &loop;

        if (!loop.IsValid()) {
            LOG_ERROR("event loop is invalid");
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::ProtocolCodec codec(config.Protocol().max_body_size);

        tinyimx::TcpServer server(
            &loop,
            listen_address,
            "protocol-echo-server"
        );

        server.SetConnectionCallback([](
            const tinyimx::TcpConnectionPtr& connection
        ) {
            if (connection->IsConnected()) {
                std::cout << "[protocol_echo_server] connected: "
                          << connection->PeerAddress().ToString()
                          << '\n';
            }
        });

        server.SetMessageCallback([&](
            const tinyimx::TcpConnectionPtr& connection,
            tinyimx::Buffer* buffer
        ) {
            const tinyimx::DecodeResult decode_result =
                codec.Decode(buffer);

            if (decode_result.status ==
                tinyimx::DecodeStatus::kNeedMoreData) {
                return;
            }

            if (decode_result.status != tinyimx::DecodeStatus::kOk) {
                LOG_WARN("protocol decode failed"
                         << ", peer="
                         << connection->PeerAddress().ToString()
                         << ", status="
                         << tinyimx::DecodeStatusToString(
                                decode_result.status)
                         << ", error="
                         << decode_result.error_message);

                tinyimx::Buffer output;
                std::string encode_error;

                const tinyimx::Packet error_packet =
                    MakeErrorPacket(0, decode_result.error_message);

                if (codec.Encode(error_packet, &output, &encode_error)) {
                    const std::string response =
                        output.RetrieveAllAsString();
                    connection->Send(response);
                }

                connection->Shutdown();
                return;
            }

            tinyimx::Buffer output;

            for (const auto& packet : decode_result.packets) {
                std::cout << "[protocol_echo_server] received packet"
                          << " type="
                          << tinyimx::MessageTypeToString(packet.type)
                          << " seq=" << packet.seq
                          << " body=" << packet.body
                          << '\n';

                const tinyimx::Packet response = HandlePacket(packet);

                std::string encode_error;
                if (!codec.Encode(response, &output, &encode_error)) {
                    LOG_ERROR("encode response failed"
                              << ", error=" << encode_error);
                    continue;
                }
            }

            if (output.ReadableBytes() > 0) {
                const std::string response =
                    output.RetrieveAllAsString();
                connection->Send(response);
            }
        });

        if (!server.Start()) {
            LOG_ERROR("protocol echo server start failed");
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        std::cout << "========== Protocol Echo Server Demo ==========\n";
        std::cout << "Listening on " << listen_address.ToString() << '\n';
        std::cout << "Run client:\n";
        std::cout << "  ./build/linux-debug/protocol_echo_client_demo "
                  << "127.0.0.1 " << config.ServerPort() << '\n';
        std::cout << "Press Ctrl-C to stop server.\n";

        LOG_INFO("protocol echo server demo started"
                 << ", listen=" << listen_address.ToString());

        loop.Loop();

        server.Stop();

        std::cout << "Protocol echo server stopped\n";
        std::cout << "===============================================\n";

        g_loop = nullptr;
    }

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}