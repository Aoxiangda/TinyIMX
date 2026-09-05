#pragma once

#include <memory>
#include <mutex>
#include <string>

namespace grpc {
class Server;
class Service;
}

namespace tinyimx::user {

class UserServiceServer final {
public:
    explicit UserServiceServer(
        grpc::Service* service
    );
    ~UserServiceServer();

    UserServiceServer(const UserServiceServer&) = delete;
    UserServiceServer& operator=(const UserServiceServer&) = delete;

    bool Start(const std::string& listen_target);
    void Shutdown();
    void Wait();

    [[nodiscard]] bool Running() const;
    [[nodiscard]] int SelectedPort() const;
    [[nodiscard]] std::string BoundTarget() const;

private:
    grpc::Service* service_{nullptr};  // non-owning

    mutable std::mutex mutex_;
    std::unique_ptr<grpc::Server> server_;
    std::string bound_target_;
    int selected_port_{0};
    bool shutdown_requested_{false};
};

}  // namespace tinyimx::user
