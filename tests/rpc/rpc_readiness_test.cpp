#include "common/runtime/RpcReadinessProbe.h"
#include "services/user/server/UserServiceServer.h"
#include "services/social/server/SocialServiceServer.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/group/server/GroupServiceServer.h"
#include "services/file/server/FileServiceServer.h"
#include "tinyimx/user/v1/user_service.grpc.pb.h"
#include "tinyimx/social/v1/social_service.grpc.pb.h"
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include "tinyimx/group/v1/group_service.grpc.pb.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <stdexcept>
namespace {
int checks = 0;
void Require(bool ok, const char* name) { if (!ok) throw std::runtime_error(name); ++checks; }
template<class Server, class Service> void Check(const char* domain) {
    Service service;
    Server server(&service);
    Require(!server.SetReady(true), "unstarted rejected");
    Require(server.Start("127.0.0.1:0"), "Start");
    const auto target = server.BoundTarget();
    Require(server.Running(), "running");
    Require(!tinyimx::runtime::CheckRpcReadiness(target), "listening but NOT_SERVING");
    for (int i = 0; i < 3; ++i) {
        Require(server.SetReady(true), "SetReady true");
        Require(tinyimx::runtime::CheckRpcReadiness(target), "SERVING");
        Require(server.SetReady(false), "SetReady false");
        Require(!tinyimx::runtime::CheckRpcReadiness(target), "draining NOT_SERVING");
    }
    server.Shutdown(); server.Wait();
    Require(!server.SetReady(true), "no readiness resurrection");
    Require(!server.Running(), "stopped");
    Require(!tinyimx::runtime::CheckRpcReadiness(target), "closed rejected");
    std::cout << "{\"domain\":\"" << domain << "\",\"status\":\"PASS\"}\n";
}
}
int main() {
    try {
        Require(!tinyimx::runtime::CheckRpcReadiness(""), "empty rejected");
        Require(!tinyimx::runtime::CheckRpcReadiness("127.0.0.1:1"), "unreachable rejected");
        Check<tinyimx::user::UserServiceServer, tinyimx::user::v1::UserService::Service>("user");
        Check<tinyimx::social::SocialServiceServer, tinyimx::social::v1::SocialService::Service>("social");
        Check<tinyimx::message::MessageServiceServer, tinyimx::message::v1::MessageService::Service>("message");
        Check<tinyimx::group::GroupServiceServer, tinyimx::group::v1::GroupService::Service>("group");
        Check<tinyimx::file::FileServiceServer, tinyimx::file::v1::FileService::Service>("file");
        std::cout << "{\"status\":\"RPC_READINESS_NATIVE_PASS\",\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
