#include "services/social/server/SocialServiceServer.h"

#include <grpcpp/grpcpp.h>

#include <string>

namespace tinyimx::social {
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

SocialServiceServer::SocialServiceServer(
    grpc::Service* service
)
    : service_(service) {
}

SocialServiceServer::~SocialServiceServer() {
    Shutdown();
}

bool SocialServiceServer::Start(
    const std::string& listen_target
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (service_ == nullptr ||
        listen_target.empty() ||
        server_ != nullptr) {
        return false;
    }

    grpc::ServerBuilder builder;
    int selected_port = 0;

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

    server_ = std::move(server);
    selected_port_ = selected_port;
    bound_target_ =
        BuildBoundTarget(listen_target, selected_port);
    shutdown_requested_ = false;
    return true;
}

void SocialServiceServer::Shutdown() {
    grpc::Server* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (server_ == nullptr ||
            shutdown_requested_) {
            return;
        }

        shutdown_requested_ = true;
        server = server_.get();
    }

    // Do not reset server_ here: another thread may currently be inside
    // Wait(). The owning object outlives that Wait in TinyIMX bootstrap.
    server->Shutdown();
}

void SocialServiceServer::Wait() {
    grpc::Server* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        server = server_.get();
    }

    if (server != nullptr) {
        server->Wait();
    }
}

bool SocialServiceServer::Running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return server_ != nullptr && !shutdown_requested_;
}

int SocialServiceServer::SelectedPort() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_port_;
}

std::string SocialServiceServer::BoundTarget() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bound_target_;
}

}  // namespace tinyimx::social
