#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyimx {

struct TelemetryIdentity {
    std::string service_name;
    std::string service_namespace{"tinyimx"};
    std::string service_instance_id;
    std::string deployment_environment{"dev"};
};

struct ThreadPoolMetricsSnapshot {
    std::int64_t queue_size{0};
    std::int64_t worker_count{0};
    std::int64_t active_worker_count{0};
};

struct TcpServerMetricsSnapshot {
    std::int64_t connection_count{0};
    std::int64_t peak_connection_count{0};
};

}  // namespace tinyimx
