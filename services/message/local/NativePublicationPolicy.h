#pragma once

#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace tinyimx::message::local {

// Native publication owns a separate bounded worker pool. SQL Outbox relay
// settings do not configure it. Missing settings preserve the frozen defaults.
struct NativePublicationPolicy {
    std::size_t workers{4};
    std::size_t max_held{64};
    std::size_t progress_batch_size{1};
};

inline NativePublicationPolicy ReadNativePublicationPolicy(const nlohmann::json& owner) {
    if (!owner.is_object()) {
        throw std::invalid_argument("native owner configuration must be an object");
    }
    NativePublicationPolicy out;
    if (!owner.contains("publication")) return out;
    const auto& config = owner.at("publication");
    if (!config.is_object()) {
        throw std::invalid_argument("native publication configuration must be an object");
    }
    for (auto it = config.begin(); it != config.end(); ++it) {
        if (it.key() != "workers" && it.key() != "max_held" && it.key() != "progress_batch_size") {
            throw std::invalid_argument("unknown native publication setting: " + it.key());
        }
    }
    auto bounded = [&](const char* key, std::size_t fallback, std::size_t high) {
        if (!config.contains(key)) return fallback;
        const auto& value = config.at(key);
        // JSON floating-point, bool and strings are never silently coerced.
        if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 1)) {
            throw std::invalid_argument(std::string("native publication ") + key + " must be a positive integer");
        }
        const auto number = value.get<std::uint64_t>();
        if (number < 1 || number > high) {
            throw std::invalid_argument(std::string("native publication ") + key + " is outside the supported bound");
        }
        return static_cast<std::size_t>(number);
    };
    out.workers = bounded("workers", out.workers, 16);
    out.max_held = bounded("max_held", out.max_held, 256);
    out.progress_batch_size = bounded("progress_batch_size", out.progress_batch_size, 16);
    if (out.max_held < out.workers) {
        throw std::invalid_argument("native publication max_held must cover all workers");
    }
    if (out.progress_batch_size > out.max_held) {
        throw std::invalid_argument("native publication progress_batch_size must fit max_held");
    }
    return out;
}

}  // namespace tinyimx::message::local
