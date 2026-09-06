#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx::registry::zookeeper {

struct ServiceInstance {
    int schema_version{1};
    std::string service_name;
    std::string instance_id;
    std::string target;
    std::string protocol{"grpc"};
    std::string version{"v1"};

    [[nodiscard]] bool Valid() const;
    [[nodiscard]] std::string Serialize() const;
    [[nodiscard]] std::string BuildPath(
        const std::string& service_root
    ) const;

    [[nodiscard]] static std::optional<ServiceInstance>
    Deserialize(const std::string& body);

    [[nodiscard]] static std::string BuildInstanceId(
        const std::string& service_name,
        const std::string& target
    );

    [[nodiscard]] static std::string BuildTarget(
        const std::string& host,
        std::uint16_t port
    );
};

}  // namespace tinyimx::registry::zookeeper
