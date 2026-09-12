#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace tinyimx::eventing {

struct DomainEvent {
    std::uint32_t schema_version{1};
    std::string event_id;
    std::string event_type;

    std::string aggregate_type;
    std::string aggregate_id;

    std::string producer_service;
    std::string occurred_at;

    std::string request_id;
    std::string trace_id;

    nlohmann::json payload = nlohmann::json::object();

    [[nodiscard]] bool Valid() const noexcept;
};

}  // namespace tinyimx::eventing
