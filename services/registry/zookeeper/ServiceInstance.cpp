#include "services/registry/zookeeper/ServiceInstance.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <nlohmann/json.hpp>

namespace tinyimx::registry::zookeeper {
namespace {

bool IsSafeServiceName(const std::string& value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }

    for (char ch : value) {
        const unsigned char byte =
            static_cast<unsigned char>(ch);
        if (std::isalnum(byte) != 0 ||
            ch == '-' || ch == '_' || ch == '.') {
            continue;
        }
        return false;
    }

    return true;
}

bool IsSafePathSegment(const std::string& value) {
    return !value.empty() &&
           value.size() <= 255 &&
           value.find('/') == std::string::npos &&
           value.find('\0') == std::string::npos;
}

bool IsValidTarget(const std::string& value) {
    if (value.empty() || value.size() > 512) {
        return false;
    }
    return value.find('/') == std::string::npos &&
           value.find('\0') == std::string::npos;
}

std::string NormalizeRoot(std::string root) {
    while (root.size() > 1 && root.back() == '/') {
        root.pop_back();
    }
    return root;
}

}  // namespace

bool ServiceInstance::Valid() const {
    return schema_version == 1 &&
           IsSafeServiceName(service_name) &&
           IsSafePathSegment(instance_id) &&
           IsValidTarget(target) &&
           protocol == "grpc" &&
           !version.empty() &&
           version.size() <= 64;
}

std::string ServiceInstance::Serialize() const {
    nlohmann::json body = {
        {"schema_version", schema_version},
        {"service_name", service_name},
        {"instance_id", instance_id},
        {"target", target},
        {"protocol", protocol},
        {"version", version},
    };
    return body.dump();
}

std::string ServiceInstance::BuildPath(
    const std::string& service_root
) const {
    const std::string root = NormalizeRoot(service_root);
    return root + "/" + service_name + "/" + instance_id;
}

std::optional<ServiceInstance> ServiceInstance::Deserialize(
    const std::string& body
) {
    try {
        const auto root = nlohmann::json::parse(body);
        ServiceInstance result;
        result.schema_version = root.at("schema_version").get<int>();
        result.service_name = root.at("service_name").get<std::string>();
        result.instance_id = root.at("instance_id").get<std::string>();
        result.target = root.at("target").get<std::string>();
        result.protocol = root.at("protocol").get<std::string>();
        result.version = root.at("version").get<std::string>();

        if (!result.Valid()) {
            return std::nullopt;
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::string ServiceInstance::BuildInstanceId(
    const std::string& service_name,
    const std::string& target
) {
    std::string sanitized = target;
    std::replace(
        sanitized.begin(),
        sanitized.end(),
        '/',
        '_'
    );
    return service_name + "@" + sanitized;
}

std::string ServiceInstance::BuildTarget(
    const std::string& host,
    std::uint16_t port
) {
    std::string rendered_host = host;
    if (host.find(':') != std::string::npos &&
        !(host.size() >= 2 && host.front() == '[' && host.back() == ']')) {
        rendered_host = "[" + host + "]";
    }
    return rendered_host + ":" + std::to_string(port);
}

}  // namespace tinyimx::registry::zookeeper
