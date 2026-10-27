#pragma once
#include <memory>
#include <string>
namespace grpc {class Service;}
namespace tinyimx {class MySqlConnectionPool;struct RocketMQConfig;}
namespace tinyimx::message {class MessageRepositoryPort;}
namespace tinyimx::message::local {
// An opt-in real Message-service composition. Caller must first stop/drain RPC
// before Shutdown, and keep the SQL pool alive until this owner is destroyed.
class NativeMessageRuntime final {
public:
    NativeMessageRuntime(MySqlConnectionPool*,MessageRepositoryPort* groups,const RocketMQConfig&,const std::string& owner_config_file);
    ~NativeMessageRuntime();
    NativeMessageRuntime(const NativeMessageRuntime&)=delete;
    NativeMessageRuntime&operator=(const NativeMessageRuntime&)=delete;
    MessageRepositoryPort* Repository()const noexcept;
    grpc::Service* SnapshotService()const noexcept;
    bool Healthy()const noexcept;
    bool Shutdown()noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
