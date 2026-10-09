#pragma once

#include "services/intelligence/mcp/McpRegistry.h"

#include <memory>
#include <string>

namespace tinyimx::mcp {

struct DispatchMetadata {
    std::string protocol_version;
    std::string method_header;
    std::string name_header;
};

class Dispatcher final {
public:
    explicit Dispatcher(std::shared_ptr<const Registry> registry);

    [[nodiscard]] DispatchResult Dispatch(
        const RequestContext& context,
        const DispatchMetadata& metadata,
        const Json& request
    ) const;

private:
    [[nodiscard]] DispatchResult DispatchRequest(
        const RequestContext& context,
        const DispatchMetadata& metadata,
        const Json& request,
        const Json& id,
        const std::string& method,
        const Json& params
    ) const;

    [[nodiscard]] static bool Authorized(
        const Principal& principal,
        const std::vector<std::string>& required_scopes,
        std::string* missing_scope
    );

    std::shared_ptr<const Registry> registry_;
};

}  // namespace tinyimx::mcp
