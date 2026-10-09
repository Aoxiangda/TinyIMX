#include "services/registry/zookeeper/ServiceInstance.h"

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

bool Expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        return false;
    }
    std::cout << "[PASS] " << name << '\n';
    return true;
}

bool TestRoundTripAndPath() {
    using tinyimx::registry::zookeeper::ServiceInstance;

    ServiceInstance instance;
    instance.service_name = "message";
    instance.target = ServiceInstance::BuildTarget(
        "10.0.2.11",
        50053
    );
    instance.instance_id = ServiceInstance::BuildInstanceId(
        instance.service_name,
        instance.target
    );
    instance.version = "v1";

    if (!Expect(instance.Valid(), "valid service instance")) {
        return false;
    }
    if (!Expect(
            instance.instance_id ==
                "message@10.0.2.11:50053",
            "deterministic instance id"
        )) {
        return false;
    }
    if (!Expect(
            instance.BuildPath("/tinyimx/services/") ==
                "/tinyimx/services/message/"
                "message@10.0.2.11:50053",
            "deterministic registration path"
        )) {
        return false;
    }

    const std::string body = instance.Serialize();
    const auto parsed = ServiceInstance::Deserialize(body);
    if (!Expect(parsed.has_value(), "service instance JSON round-trip")) {
        return false;
    }
    if (!Expect(
            parsed->service_name == instance.service_name &&
            parsed->instance_id == instance.instance_id &&
            parsed->target == instance.target &&
            parsed->protocol == "grpc" &&
            parsed->version == "v1",
            "service instance fields preserved"
        )) {
        return false;
    }

    const auto json = nlohmann::json::parse(body);
    if (!Expect(json.size() == 6, "registry metadata schema minimized")) {
        return false;
    }
    for (const char* forbidden : {
             "user_id",
             "message_id",
             "packet_seq",
             "delivery_seq",
             "session_epoch",
             "connection_id",
             "gateway_id",
             "unread_count",
         }) {
        if (!Expect(
                !json.contains(forbidden),
                std::string("registry metadata excludes ") + forbidden
            )) {
            return false;
        }
    }

    return true;
}

bool TestValidation() {
    using tinyimx::registry::zookeeper::ServiceInstance;

    ServiceInstance invalid;
    invalid.service_name = "bad/service";
    invalid.instance_id = "x";
    invalid.target = "127.0.0.1:1";
    if (!Expect(!invalid.Valid(), "reject service name path injection")) {
        return false;
    }

    invalid.service_name = "user";
    invalid.instance_id = "bad/instance";
    if (!Expect(!invalid.Valid(), "reject instance path injection")) {
        return false;
    }

    invalid.instance_id = "user@127.0.0.1:50052";
    invalid.target.clear();
    if (!Expect(!invalid.Valid(), "reject empty target")) {
        return false;
    }

    return Expect(
        ServiceInstance::BuildTarget("::1", 50052) ==
            "[::1]:50052",
        "IPv6 advertise target bracketed"
    );
}

}  // namespace

int main() {
    std::cout << "========== TinyIMX M15-A ServiceInstance Tests ==========\n";
    const bool ok = TestRoundTripAndPath() && TestValidation();
    std::cout << "=========================================================\n";
    if (!ok) {
        return 1;
    }
    std::cout << "[PASS] M15-A ServiceInstance tests\n";
    return 0;
}
