#include "services/eventing/EventCodec.h"

#include <exception>

namespace tinyimx::eventing {

EventEncodeResult EventCodec::Encode(
    const DomainEvent& event
) {
    EventEncodeResult result;

    if (!event.Valid()) {
        result.message = "domain event validation failed";
        return result;
    }

    try {
        nlohmann::json envelope = {
            {"schema_version", event.schema_version},
            {"event_id", event.event_id},
            {"event_type", event.event_type},
            {"aggregate_type", event.aggregate_type},
            {"aggregate_id", event.aggregate_id},
            {"producer_service", event.producer_service},
            {"occurred_at", event.occurred_at},
            {"request_id", event.request_id},
            {"trace_id", event.trace_id},
            {"payload", event.payload},
        };

        result.encoded = envelope.dump();
        result.success = true;
        result.message = "domain event encoded";
        return result;
    } catch (const std::exception& e) {
        result.message =
            std::string("domain event encoding failed: ") + e.what();
        return result;
    }
}


EventDecodeResult EventCodec::Decode(
    const std::string& encoded
) {
    EventDecodeResult result;

    if (encoded.empty()) {
        result.message = "domain event decoding failed: empty payload";
        return result;
    }

    try {
        const auto envelope = nlohmann::json::parse(encoded);
        if (!envelope.is_object()) {
            result.message = "domain event decoding failed: envelope is not an object";
            return result;
        }

        DomainEvent event;
        event.schema_version = envelope.at("schema_version").get<std::uint32_t>();
        event.event_id = envelope.at("event_id").get<std::string>();
        event.event_type = envelope.at("event_type").get<std::string>();
        event.aggregate_type = envelope.at("aggregate_type").get<std::string>();
        event.aggregate_id = envelope.at("aggregate_id").get<std::string>();
        event.producer_service = envelope.at("producer_service").get<std::string>();
        event.occurred_at = envelope.at("occurred_at").get<std::string>();
        event.request_id = envelope.value("request_id", std::string{});
        event.trace_id = envelope.value("trace_id", std::string{});
        event.payload = envelope.at("payload");

        if (!event.Valid()) {
            result.message = "domain event decoding failed: decoded event is invalid";
            return result;
        }

        result.success = true;
        result.event = std::move(event);
        result.message = "domain event decoded";
        return result;
    } catch (const std::exception& e) {
        result.message =
            std::string("domain event decoding failed: ") + e.what();
        return result;
    }
}

}  // namespace tinyimx::eventing
