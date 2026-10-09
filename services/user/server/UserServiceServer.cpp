#include "services/user/server/UserServiceServer.h"

#include "common/runtime/RpcReadiness.h"

#include <grpcpp/grpcpp.h>

#include <string>

namespace tinyimx::user {
namespace {

std::string BuildBoundTarget(
    const std::string& requested,
    int selected_port
) {
    if (selected_port <= 0) {
        return {};
    }

    const std::size_t colon = requested.rfind(':');
    if (colon == std::string::npos) {
        return requested;
    }

    return requested.substr(0, colon + 1) +
           std::to_string(selected_port);
}

}  // namespace

UserServiceServer::UserServiceServer(
    grpc::Service* service
)
    : service_(service) {
}

UserServiceServer::~UserServiceServer() {
    Shutdown();
}

bool UserServiceServer::Start(
    const std::string& listen_target,
    UserServiceServerOptions options
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (service_ == nullptr ||
        listen_target.empty() ||
        server_ != nullptr) {
        return false;
    }

    if (options.sync_num_cqs <= 0 ||
        options.sync_min_pollers <= 0 ||
        options.sync_max_pollers < options.sync_min_pollers) {
        return false;
    }

    tinyimx::runtime::EnableRpcReadiness();
    grpc::ServerBuilder builder;
    int selected_port = 0;

    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::NUM_CQS,
        options.sync_num_cqs
    );
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::MIN_POLLERS,
        options.sync_min_pollers
    );
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::MAX_POLLERS,
        options.sync_max_pollers
    );

    builder.AddListeningPort(
        listen_target,
        grpc::InsecureServerCredentials(),
        &selected_port
    );
    builder.RegisterService(service_);

    auto server = builder.BuildAndStart();

    if (!server || selected_port <= 0) {
        return false;
    }

    if (!tinyimx::runtime::SetRpcReadiness(server.get(), false)) {
        server->Shutdown();
        return false;
    }
    server_ = std::move(server);
    selected_port_ = selected_port;
    bound_target_ =
        BuildBoundTarget(listen_target, selected_port);
    shutdown_requested_ = false;
    return true;
}

bool UserServiceServer::SetReady(bool ready) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server_ == nullptr || shutdown_requested_) return false;
    return tinyimx::runtime::SetRpcReadiness(server_.get(), ready);
}

void UserServiceServer::Shutdown() {
    grpc::Server* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (server_ == nullptr ||
            shutdown_requested_) {
            return;
        }

        tinyimx::runtime::SetRpcReadiness(server_.get(), false);
        shutdown_requested_ = true;
        server = server_.get();
    }

    // Keep server_ alive while another thread may be inside Wait().
    server->Shutdown();
}

void UserServiceServer::Wait() {
    grpc::Server* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        server = server_.get();
    }

    if (server != nullptr) {
        server->Wait();
    }
}

bool UserServiceServer::Running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return server_ != nullptr && !shutdown_requested_;
}

int UserServiceServer::SelectedPort() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_port_;
}

std::string UserServiceServer::BoundTarget() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bound_target_;
}

}  // namespace tinyimx::user
